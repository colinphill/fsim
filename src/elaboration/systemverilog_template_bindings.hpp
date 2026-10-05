// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"
#include "fsim/runtime/simir.hpp"
#include "scoped_bindings.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

struct SystemVerilogTemplateElementSignalBinding {
    runtime::simir::ContainerType container_type;
    std::uint32_t ordinal { };
    runtime::simir::SignalId signal { };
    bool readable { };
    bool writable { };
    bool read_only { };
};

struct SystemVerilogTemplateSignalRole {
    semantic::DeclarationId declaration;
    runtime::simir::SignalId signal { };
    std::optional<std::uint32_t> container_element_ordinal;
    std::optional<runtime::simir::ContainerType> container_type;
    bool read_only { };
    bool readable { true };
    bool writable { true };
};

[[nodiscard]] std::optional<
    std::vector<SystemVerilogTemplateElementSignalBinding>>
resolve_systemverilog_template_element_signals(
    semantic::DeclarationId declaration,
    const semantic::SpecializedHirOverlay& overlay,
    const elaboration_detail::ContainerDeclarationBindings& bindings,
    std::span<const ContainerObjectInfo> object_info,
    std::span<const runtime::simir::ContainerObject> objects,
    std::span<const runtime::simir::ContainerElementSignalAlias>
        element_aliases,
    std::span<const runtime::simir::ContainerAggregateSignalAlias>
        aggregate_aliases,
    std::span<const SignalInfo> signal_info,
    std::span<const runtime::simir::Signal> signals);

[[nodiscard]] std::optional<std::map<runtime::simir::SignalId,
                                      runtime::simir::SignalId>>
make_systemverilog_template_signal_remap(
    std::span<const SystemVerilogTemplateSignalRole> source_roles,
    std::span<const SystemVerilogTemplateSignalRole> target_roles,
    std::span<const SignalInfo> signal_info,
    std::span<const runtime::simir::Signal> signals);

[[nodiscard]] bool remap_systemverilog_template_process(
    runtime::simir::Process& process,
    const std::map<runtime::simir::SignalId,
                   runtime::simir::SignalId>& signal_remap,
    std::string_view from_hierarchy,
    std::string_view to_hierarchy,
    std::span<const SignalInfo> signal_info);

} // namespace fsim::elaboration
