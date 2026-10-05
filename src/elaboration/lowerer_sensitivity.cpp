// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <limits>

namespace fsim::elaboration {

void Lowerer::record_implicit_signal_dependency(SignalId signal,
    std::uint32_t offset, std::uint32_t width)
{
    if (signal < design_.signal_info_.size()
        && offset == 0U && width == design_.signal_info_[signal].width) {
        width = 0U;
    }
    implicit_signal_dependencies_.push_back(
        { signal, runtime::simir::EdgeKind::any, offset, width });
}

void Lowerer::record_container_object_dependency(
    const runtime::simir::ContainerObjectId object)
{
    bool found_element_alias = false;
    for (const auto& alias : design_.container_element_signal_aliases_) {
        if (alias.object == object && alias.readable) {
            record_implicit_signal_dependency(alias.signal);
            found_element_alias = true;
        }
    }
    if (found_element_alias) {
        return;
    }
    const auto alias = std::ranges::find_if(
        design_.container_signal_aliases_.rbegin(),
        design_.container_signal_aliases_.rend(),
        [object](const runtime::simir::ContainerSignalAlias& candidate) {
            return candidate.object == object && candidate.readable;
        });
    if (alias != design_.container_signal_aliases_.rend()) {
        record_implicit_signal_dependency(alias->signal);
    }
}

std::optional<runtime::simir::Sensitivity>
Lowerer::hir_static_signal_sensitivity(semantic::ExpressionId expression_id,
    semantic::ScopeId process_scope)
{
    using runtime::simir::EdgeKind;
    using runtime::simir::Sensitivity;
    // Explicit sensitivity is inspected before lower_hir_process_body installs
    // its scope. Container binding helpers use that member scope internally.
    struct ScopeRestore {
        semantic::ScopeId& current;
        semantic::ScopeId previous;
        ~ScopeRestore() { current = previous; }
    } restore { hir_process_scope_, hir_process_scope_ };
    hir_process_scope_ = process_scope;
    if (specialized_hir_unit_ == nullptr)
        return std::nullopt;
    const auto expression = specialized_hir_unit_->find_expression(expression_id);
    if (!expression || expression->systemverilog == nullptr)
        return std::nullopt;

    // This path retains the logical array but selects its exact flattened net
    // interval. Element-net lowering can supply the same contract with offset 0.
    if (const auto element = hir_container_element_binding(expression_id)) {
        if (const auto selected = hir_static_container_signal_extract(*element)) {
            return Sensitivity { selected->signal, EdgeKind::any,
                selected->offset, selected->width };
        }
        // A dynamic unpacked index depends conservatively on the entire array.
        return std::nullopt;
    }

    const auto& source = *expression->systemverilog;
    if (source.kind == semantic::sv::ExpressionKind::index
        || source.kind == semantic::sv::ExpressionKind::slice) {
        const auto selection = hir_constant_selection(expression_id, process_scope);
        if (!selection || source.operands.empty())
            return std::nullopt;
        auto parent = hir_static_signal_sensitivity(
            source.operands.front(), process_scope);
        if (!parent || parent->signal >= design_.signal_info_.size())
            return std::nullopt;
        const auto parent_width = parent->width != 0U
            ? parent->width : design_.signal_info_[parent->signal].width;
        if (selection->offset >= parent_width
            || selection->width == 0U
            || selection->width > parent_width - selection->offset
            || selection->offset
                > std::numeric_limits<std::uint32_t>::max() - parent->offset
            || selection->width > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        parent->offset += static_cast<std::uint32_t>(selection->offset);
        parent->width = static_cast<std::uint32_t>(selection->width);
        return parent;
    }

    // A declaration reference embedded in arithmetic/calls is insufficient:
    // carries and side effects can depend on bits outside the selected result.
    if (source.kind != semantic::sv::ExpressionKind::name)
        return std::nullopt;
    if (const auto signal = hir_direct_signal(expression_id))
        return Sensitivity { *signal, EdgeKind::any };
    const auto declaration = hir_referenced_declaration(expression_id);
    const auto binding = declaration
        ? hir_runtime_binding(*declaration, process_scope, false)
        : std::nullopt;
    if (binding && binding->signal)
        return Sensitivity { *binding->signal, EdgeKind::any };
    return std::nullopt;
}

} // namespace fsim::elaboration
