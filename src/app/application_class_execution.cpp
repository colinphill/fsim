// SPDX-License-Identifier: Apache-2.0
#include "application_class_execution.hpp"

#include <algorithm>
#include <utility>

namespace fsim::app::application_detail {
SystemVerilogClassExecution::SystemVerilogClassExecution(
    runtime::SystemVerilogClassHeap& heap,
    runtime::SystemVerilogUvmComponentService& components,
    runtime::SystemVerilogUvmPhaseService& phases,
    std::map<std::string, runtime::SystemVerilogUvmRootHandle, std::less<>>&
        roots_by_scope)
    : heap_(heap)
    , components_(components)
    , phases_(phases)
    , roots_by_scope_(roots_by_scope)
{
}

runtime::SystemVerilogUvmRootHandle
SystemVerilogClassExecution::component_root(
    const std::string_view allocation_scope)
{
    const std::string identity {
        allocation_scope.empty() ? "$simulation" : allocation_scope
    };
    const auto found = roots_by_scope_.find(identity);
    if (found != roots_by_scope_.end())
        return found->second;
    const auto root = components_.create_root(identity);
    const auto [inserted, did_insert] = roots_by_scope_.emplace(identity, root);
    if (!did_insert) {
        components_.destroy_root(root);
        throw std::logic_error { "duplicate automatic UVM root identity" };
    }
    try {
        phases_.participate_standard_root(root);
    } catch (...) {
        roots_by_scope_.erase(inserted);
        components_.destroy_root(root);
        throw;
    }
    return root;
}

runtime::PackedLogic4
SystemVerilogClassExecution::invoke_source_randomization_mode(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view method,
    const std::span<const runtime::PackedLogic4> actuals)
{
    return application_detail::invoke_systemverilog_randomization_mode(
        heap_, handle, method, actuals);
}

runtime::PackedLogic4 SystemVerilogClassExecution::resize_packed(
    const runtime::PackedLogic4& value,
    const std::size_t width)
{
    runtime::PackedLogic4 result(width, runtime::Logic4::zero);
    for (std::size_t bit = 0; bit < std::min(width, value.width()); ++bit)
        result.set(bit, value.get(bit));
    return result;
}

runtime::PackedLogic4 SystemVerilogClassExecution::packed_property_value(
    const runtime::SystemVerilogClassPropertyValue& value)
{
    if (value.kind == runtime::SystemVerilogClassPropertyKind::ClassHandle)
        return runtime::PackedLogic4::from_aval_bval(64, value.handle, 0);
    return value.packed;
}

runtime::PackedLogic4 SystemVerilogClassExecution::invoke_class_container(
    const runtime::SystemVerilogClassHandle receiver,
    const std::string_view operation,
    const std::span<const runtime::PackedLogic4> actuals)
{
    constexpr std::string_view read_prefix { "@container-read:" };
    constexpr std::string_view write_prefix { "@container-write:" };
    constexpr std::string_view resize_prefix { "@container-resize:" };
    constexpr std::string_view push_back_prefix {
        "@container-push-back:"
    };
    constexpr std::string_view pop_front_prefix {
        "@container-pop-front:"
    };
    constexpr std::string_view size_prefix { "@container-size:" };
    const auto prefix = operation.starts_with(read_prefix)
        ? read_prefix
        : operation.starts_with(write_prefix)
        ? write_prefix
        : operation.starts_with(resize_prefix)
        ? resize_prefix
        : operation.starts_with(push_back_prefix)
        ? push_back_prefix
        : operation.starts_with(pop_front_prefix)
        ? pop_front_prefix
        : operation.starts_with(size_prefix)
        ? size_prefix
        : std::string_view { };
    if (prefix.empty()) {
        throw std::invalid_argument {
            "unknown class handle container operation"
        };
    }
    auto& property = heap_.property(receiver, operation.substr(prefix.size()));
    if (!property.handle_container) {
        throw std::invalid_argument {
            "class property is not a handle container"
        };
    }
    auto& container = *property.handle_container;
    const auto known_word = [&](const std::size_t index) {
        if (index >= actuals.size()) {
            throw std::invalid_argument {
                "class handle container operation has missing actuals"
            };
        }
        const auto word = actuals[index].low_word();
        if (word.bval != 0) {
            throw std::invalid_argument {
                "class handle container operand contains X or Z"
            };
        }
        return word.aval;
    };
    if (prefix == resize_prefix) {
        if (actuals.size() != 1U
            || !std::in_range<std::size_t>(known_word(0))) {
            throw std::length_error {
                "class handle container size exceeds host storage"
            };
        }
        container.resize(static_cast<std::size_t>(known_word(0)));
        return runtime::PackedLogic4::from_aval_bval(64, 0, 0);
    }
    if (prefix == push_back_prefix) {
        if (actuals.size() != 1U) {
            throw std::invalid_argument {
                "class handle queue push_back has inconsistent actuals"
            };
        }
        const auto value = known_word(0);
        container.push_back(heap_, value);
        return runtime::PackedLogic4::from_aval_bval(64, value, 0);
    }
    if (prefix == pop_front_prefix) {
        if (!actuals.empty()) {
            throw std::invalid_argument {
                "class handle queue pop_front has inconsistent actuals"
            };
        }
        return runtime::PackedLogic4::from_aval_bval(
            64, container.pop_front(), 0);
    }
    if (prefix == size_prefix) {
        if (!actuals.empty()) {
            throw std::invalid_argument {
                "class handle container size has inconsistent actuals"
            };
        }
        return runtime::PackedLogic4::from_aval_bval(
            32, container.size(), 0);
    }
    if (actuals.size() != (prefix == read_prefix ? 1U : 2U)) {
        throw std::invalid_argument {
            "class handle container operation has inconsistent actuals"
        };
    }
    const auto index = known_word(0);
    const auto keyed = container.kind()
            == runtime::SystemVerilogClassContainerKind::AssociativeArray
        || container.kind()
            == runtime::SystemVerilogClassContainerKind::UnpackedAggregate;
    if (prefix == read_prefix) {
        const auto value = keyed
            ? container.at(std::to_string(index))
            : container.at(static_cast<std::size_t>(index));
        return runtime::PackedLogic4::from_aval_bval(64, value, 0);
    }
    const auto value = known_word(1);
    if (keyed)
        container.set(heap_, std::to_string(index), value);
    else
        container.set(heap_, static_cast<std::size_t>(index), value);
    return runtime::PackedLogic4::from_aval_bval(64, value, 0);
}

runtime::PackedLogic4
SystemVerilogClassExecution::invoke_checked_class_cast(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view operation) const
{
    constexpr std::string_view prefix { "@checked-cast:" };
    try {
        (void)heap_.checked_cast(handle, operation.substr(prefix.size()));
        return runtime::PackedLogic4::from_aval_bval(1, 1, 0);
    } catch (const std::invalid_argument&) {
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
}

std::pair<std::string_view, std::string_view>
SystemVerilogClassExecution::static_property_parts(const std::string_view identity)
{
    const auto separator = identity.rfind("::");
    if (separator == std::string_view::npos
        || separator == 0 || separator + 2U >= identity.size()) {
        throw std::invalid_argument {
            "class static property identity is malformed"
        };
    }
    return { identity.substr(0, separator), identity.substr(separator + 2U) };
}

} // namespace fsim::app::application_detail
