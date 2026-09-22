// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"

namespace fsim::app::application_detail {

class SystemVerilogClassExecution {
public:
    SystemVerilogClassExecution(
        runtime::SystemVerilogClassHeap& heap,
        runtime::SystemVerilogUvmComponentService& components,
        runtime::SystemVerilogUvmPhaseService& phases,
        std::map<std::string, runtime::SystemVerilogUvmRootHandle,
            std::less<>>& roots_by_scope);

    [[nodiscard]] runtime::SystemVerilogUvmRootHandle component_root(
        std::string_view allocation_scope);
    [[nodiscard]] runtime::PackedLogic4 invoke_source_randomization_mode(
        runtime::SystemVerilogClassHandle handle,
        std::string_view method,
        std::span<const runtime::PackedLogic4> actuals);
    [[nodiscard]] static runtime::PackedLogic4 resize_packed(
        const runtime::PackedLogic4& value,
        std::size_t width);
    [[nodiscard]] static runtime::PackedLogic4 packed_property_value(
        const runtime::SystemVerilogClassPropertyValue& value);
    [[nodiscard]] runtime::PackedLogic4 invoke_class_container(
        runtime::SystemVerilogClassHandle receiver,
        std::string_view operation,
        std::span<const runtime::PackedLogic4> actuals);
    [[nodiscard]] runtime::PackedLogic4 invoke_checked_class_cast(
        runtime::SystemVerilogClassHandle handle,
        std::string_view operation) const;
    [[nodiscard]] static std::pair<std::string_view, std::string_view>
    static_property_parts(std::string_view identity);

private:
    runtime::SystemVerilogClassHeap& heap_;
    runtime::SystemVerilogUvmComponentService& components_;
    runtime::SystemVerilogUvmPhaseService& phases_;
    std::map<std::string, runtime::SystemVerilogUvmRootHandle, std::less<>>&
        roots_by_scope_;
};

} // namespace fsim::app::application_detail
