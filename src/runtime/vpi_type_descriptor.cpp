// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_type_descriptor.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <string_view>

namespace fsim::runtime {

namespace {

    constexpr std::size_t maximum_depth = 64;
    constexpr std::size_t maximum_nodes = 65'536;
    constexpr std::size_t maximum_elements = 65'536;
    constexpr std::size_t maximum_name_size = 4096;

    bool checked_add(
        const std::size_t left,
        const std::size_t right,
        std::size_t& result)
    {
        if (left > std::numeric_limits<std::size_t>::max() - right) {
            return false;
        }
        result = left + right;
        return true;
    }

    bool checked_multiply(
        const std::size_t left,
        const std::size_t right,
        std::size_t& result)
    {
        if (left != 0U
            && right > std::numeric_limits<std::size_t>::max() / left) {
            return false;
        }
        result = left * right;
        return true;
    }

    bool valid_name(const std::string_view name)
    {
        return !name.empty() && name.size() <= maximum_name_size
            && name.find('\0') == std::string_view::npos;
    }

    bool valid_scalar(
        const SystemVerilogVpiValueCategory category,
        const std::uint32_t width,
        const bool is_signed)
    {
        switch (category) {
        case SystemVerilogVpiValueCategory::Bit2:
        case SystemVerilogVpiValueCategory::Logic4:
        case SystemVerilogVpiValueCategory::Logic9:
        case SystemVerilogVpiValueCategory::Integer2:
        case SystemVerilogVpiValueCategory::Integer4:
            return width != 0U;
        case SystemVerilogVpiValueCategory::Real:
            return width == 64U && !is_signed;
        case SystemVerilogVpiValueCategory::ShortReal:
            return width == 32U && !is_signed;
        case SystemVerilogVpiValueCategory::Time:
            return width == 64U && !is_signed;
        default:
            return false;
        }
    }

    bool no_shape_extras(const SystemVerilogVpiTypeDescriptor& descriptor)
    {
        return descriptor.nominal_name.empty() && descriptor.ranges.empty()
            && !descriptor.maximum_size && descriptor.children.empty()
            && descriptor.member_names.empty()
            && descriptor.enum_literals.empty();
    }
    bool valid_packed_element(
        const SystemVerilogVpiTypeDescriptor& descriptor)
    {
        if (descriptor.kind == SystemVerilogVpiDescriptorKind::Enum) {
            return true;
        }
        if (descriptor.kind == SystemVerilogVpiDescriptorKind::Scalar) {
            return descriptor.category == SystemVerilogVpiValueCategory::Bit2
                || descriptor.category == SystemVerilogVpiValueCategory::Logic4
                || descriptor.category == SystemVerilogVpiValueCategory::Logic9
                || descriptor.category == SystemVerilogVpiValueCategory::Integer2
                || descriptor.category == SystemVerilogVpiValueCategory::Integer4;
        }
        return descriptor.kind == SystemVerilogVpiDescriptorKind::PackedArray
            && descriptor.children.size() == 1U
            && valid_packed_element(descriptor.children.front());
    }

    SystemVerilogVpiDescriptorError measure(
        const SystemVerilogVpiTypeDescriptor& descriptor,
        const std::size_t depth,
        SystemVerilogVpiDescriptorLayout& layout)
    {
        if (depth > maximum_depth) {
            return SystemVerilogVpiDescriptorError::DepthLimit;
        }
        if (static_cast<unsigned>(descriptor.kind)
                > static_cast<unsigned>(
                    SystemVerilogVpiDescriptorKind::ClassHandle)
            || static_cast<unsigned>(descriptor.category)
                > static_cast<unsigned>(SystemVerilogVpiValueCategory::Event)) {
            return SystemVerilogVpiDescriptorError::InvalidKind;
        }
        layout = { 1, 0, 0, depth, false };

        const auto merge_child = [&](const SystemVerilogVpiDescriptorLayout& child)
            -> SystemVerilogVpiDescriptorError {
            if (!checked_add(layout.node_count, child.node_count, layout.node_count)) {
                return SystemVerilogVpiDescriptorError::ArithmeticOverflow;
            }
            if (layout.node_count > maximum_nodes) {
                return SystemVerilogVpiDescriptorError::NodeLimit;
            }
            layout.maximum_depth = std::max(
                layout.maximum_depth, child.maximum_depth);
            layout.dynamic = layout.dynamic || child.dynamic;
            return SystemVerilogVpiDescriptorError::None;
        };

        switch (descriptor.kind) {
        case SystemVerilogVpiDescriptorKind::Scalar:
            if (!valid_scalar(
                    descriptor.category, descriptor.width, descriptor.is_signed)
                || !no_shape_extras(descriptor)) {
                return SystemVerilogVpiDescriptorError::InvalidShape;
            }
            layout.fixed_element_count = 1;
            layout.fixed_bits = descriptor.width;
            return SystemVerilogVpiDescriptorError::None;

        case SystemVerilogVpiDescriptorKind::String:
            if (descriptor.category != SystemVerilogVpiValueCategory::String
                || descriptor.width != 0U || descriptor.is_signed
                || !no_shape_extras(descriptor)) {
                return SystemVerilogVpiDescriptorError::InvalidShape;
            }
            layout.fixed_element_count = 1;
            layout.dynamic = true;
            return SystemVerilogVpiDescriptorError::None;

        case SystemVerilogVpiDescriptorKind::Enum: {
            if (!valid_scalar(
                    descriptor.category, descriptor.width, descriptor.is_signed)
                || descriptor.width > 64U || !descriptor.ranges.empty()
                || descriptor.maximum_size || !descriptor.children.empty()
                || !descriptor.member_names.empty()
                || descriptor.enum_literals.empty()) {
                return SystemVerilogVpiDescriptorError::InvalidEnum;
            }
            std::set<std::string> names;
            std::set<std::int64_t> values;
            std::int64_t minimum { };
            std::int64_t maximum { };
            if (descriptor.is_signed) {
                if (descriptor.width == 64U) {
                    minimum = std::numeric_limits<std::int64_t>::min();
                    maximum = std::numeric_limits<std::int64_t>::max();
                } else {
                    const auto magnitude = std::int64_t { 1 } << (descriptor.width - 1U);
                    minimum = -magnitude;
                    maximum = magnitude - 1;
                }
            } else {
                minimum = 0;
                maximum = descriptor.width == 64U
                    ? std::numeric_limits<std::int64_t>::max()
                    : static_cast<std::int64_t>(
                          (std::uint64_t { 1 } << descriptor.width) - 1U);
            }
            for (const auto& literal : descriptor.enum_literals) {
                if (!valid_name(literal.name)
                    || !names.insert(literal.name).second
                    || !values.insert(literal.value).second
                    || literal.value < minimum || literal.value > maximum) {
                    return SystemVerilogVpiDescriptorError::InvalidEnum;
                }
            }
            layout.fixed_element_count = 1;
            layout.fixed_bits = descriptor.width;
            return SystemVerilogVpiDescriptorError::None;
        }

        case SystemVerilogVpiDescriptorKind::PackedArray:
        case SystemVerilogVpiDescriptorKind::UnpackedArray: {
            if (descriptor.category != SystemVerilogVpiValueCategory::None
                || descriptor.width != 0U || descriptor.is_signed
                || !descriptor.nominal_name.empty() || descriptor.ranges.empty()
                || descriptor.maximum_size || descriptor.children.size() != 1U
                || !descriptor.member_names.empty()
                || !descriptor.enum_literals.empty()) {
                return SystemVerilogVpiDescriptorError::InvalidShape;
            }
            SystemVerilogVpiDescriptorLayout element;
            const auto error = measure(
                descriptor.children.front(), depth + 1U, element);
            if (error != SystemVerilogVpiDescriptorError::None) {
                return error;
            }
            if (descriptor.kind == SystemVerilogVpiDescriptorKind::PackedArray
                && (element.dynamic
                    || !valid_packed_element(descriptor.children.front()))) {
                return SystemVerilogVpiDescriptorError::InvalidShape;
            }
            if (const auto merge_error = merge_child(element);
                merge_error != SystemVerilogVpiDescriptorError::None) {
                return merge_error;
            }
            std::size_t count = 1;
            for (const auto& range : descriptor.ranges) {
                const auto distance = range.left >= range.right
                    ? static_cast<std::uint64_t>(range.left)
                        - static_cast<std::uint64_t>(range.right)
                    : static_cast<std::uint64_t>(range.right)
                        - static_cast<std::uint64_t>(range.left);
                if (distance == std::numeric_limits<std::uint64_t>::max()) {
                    return SystemVerilogVpiDescriptorError::ArithmeticOverflow;
                }
                const auto range_size = distance + 1U;
                if ((descriptor.kind
                            == SystemVerilogVpiDescriptorKind::UnpackedArray
                        && range_size > maximum_elements)
                    || !checked_multiply(
                        count, static_cast<std::size_t>(range_size), count)) {
                    return SystemVerilogVpiDescriptorError::ElementLimit;
                }
            }
            if ((descriptor.kind
                        == SystemVerilogVpiDescriptorKind::UnpackedArray
                    && count > maximum_elements)
                || !checked_multiply(
                    element.fixed_element_count,
                    count,
                    layout.fixed_element_count)
                || (descriptor.kind
                        == SystemVerilogVpiDescriptorKind::UnpackedArray
                    && layout.fixed_element_count > maximum_elements)
                || !checked_multiply(
                    element.fixed_bits, count, layout.fixed_bits)) {
                return SystemVerilogVpiDescriptorError::ElementLimit;
            }
            if (element.dynamic) {
                layout.fixed_element_count = 0;
                layout.fixed_bits = 0;
                layout.dynamic = true;
            }
            return SystemVerilogVpiDescriptorError::None;
        }

        case SystemVerilogVpiDescriptorKind::DynamicArray:
        case SystemVerilogVpiDescriptorKind::Queue: {
            if (descriptor.category != SystemVerilogVpiValueCategory::None
                || descriptor.width != 0U || descriptor.is_signed
                || !descriptor.nominal_name.empty() || !descriptor.ranges.empty()
                || descriptor.children.size() != 1U
                || !descriptor.member_names.empty()
                || !descriptor.enum_literals.empty()
                || (descriptor.maximum_size
                    && (*descriptor.maximum_size == 0U
                        || *descriptor.maximum_size > maximum_elements))) {
                return SystemVerilogVpiDescriptorError::InvalidShape;
            }
            SystemVerilogVpiDescriptorLayout element;
            const auto error = measure(
                descriptor.children.front(), depth + 1U, element);
            if (error != SystemVerilogVpiDescriptorError::None) {
                return error;
            }
            if (const auto merge_error = merge_child(element);
                merge_error != SystemVerilogVpiDescriptorError::None) {
                return merge_error;
            }
            layout.fixed_element_count = 0;
            layout.fixed_bits = 0;
            layout.dynamic = true;
            return SystemVerilogVpiDescriptorError::None;
        }

        case SystemVerilogVpiDescriptorKind::AssociativeArray: {
            if (descriptor.category != SystemVerilogVpiValueCategory::None
                || descriptor.width != 0U || descriptor.is_signed
                || !descriptor.nominal_name.empty() || !descriptor.ranges.empty()
                || descriptor.children.size() != 2U
                || !descriptor.member_names.empty()
                || !descriptor.enum_literals.empty()
                || (descriptor.maximum_size
                    && (*descriptor.maximum_size == 0U
                        || *descriptor.maximum_size > maximum_elements))) {
                return SystemVerilogVpiDescriptorError::InvalidShape;
            }
            for (const auto& child_descriptor : descriptor.children) {
                SystemVerilogVpiDescriptorLayout child;
                const auto error = measure(
                    child_descriptor, depth + 1U, child);
                if (error != SystemVerilogVpiDescriptorError::None) {
                    return error;
                }
                if (const auto merge_error = merge_child(child);
                    merge_error != SystemVerilogVpiDescriptorError::None) {
                    return merge_error;
                }
            }
            if (descriptor.children.front().kind
                    != SystemVerilogVpiDescriptorKind::Scalar
                && descriptor.children.front().kind
                    != SystemVerilogVpiDescriptorKind::Enum
                && descriptor.children.front().kind
                    != SystemVerilogVpiDescriptorKind::String) {
                return SystemVerilogVpiDescriptorError::InvalidShape;
            }
            layout.fixed_element_count = 0;
            layout.fixed_bits = 0;
            layout.dynamic = true;
            return SystemVerilogVpiDescriptorError::None;
        }

        case SystemVerilogVpiDescriptorKind::Struct:
        case SystemVerilogVpiDescriptorKind::Union:
        case SystemVerilogVpiDescriptorKind::Class: {
            const bool class_type = descriptor.kind == SystemVerilogVpiDescriptorKind::Class;
            if (descriptor.category != SystemVerilogVpiValueCategory::None
                || descriptor.width != 0U || descriptor.is_signed
                || !descriptor.ranges.empty() || descriptor.maximum_size
                || descriptor.children.empty()
                || descriptor.children.size() != descriptor.member_names.size()
                || !descriptor.enum_literals.empty()
                || (class_type
                        ? !valid_name(descriptor.nominal_name)
                        : !descriptor.nominal_name.empty())) {
                return SystemVerilogVpiDescriptorError::InvalidShape;
            }
            std::set<std::string> names;
            for (const auto& name : descriptor.member_names) {
                if (!valid_name(name) || !names.insert(name).second) {
                    return SystemVerilogVpiDescriptorError::InvalidName;
                }
            }
            layout.fixed_element_count = 0;
            layout.fixed_bits = 0;
            for (const auto& child_descriptor : descriptor.children) {
                SystemVerilogVpiDescriptorLayout child;
                const auto error = measure(
                    child_descriptor, depth + 1U, child);
                if (error != SystemVerilogVpiDescriptorError::None) {
                    return error;
                }
                if (const auto merge_error = merge_child(child);
                    merge_error != SystemVerilogVpiDescriptorError::None) {
                    return merge_error;
                }
                if (!class_type && !child.dynamic) {
                    if (descriptor.kind == SystemVerilogVpiDescriptorKind::Struct) {
                        if (!checked_add(
                                layout.fixed_element_count,
                                child.fixed_element_count,
                                layout.fixed_element_count)
                            || !checked_add(
                                layout.fixed_bits,
                                child.fixed_bits,
                                layout.fixed_bits)) {
                            return SystemVerilogVpiDescriptorError::ArithmeticOverflow;
                        }
                    } else {
                        layout.fixed_element_count = std::max(
                            layout.fixed_element_count, child.fixed_element_count);
                        layout.fixed_bits = std::max(layout.fixed_bits, child.fixed_bits);
                    }
                }
            }
            if (class_type) {
                layout.dynamic = true;
                layout.fixed_element_count = 0;
                layout.fixed_bits = 0;
            } else if (layout.dynamic) {
                layout.fixed_element_count = 0;
                layout.fixed_bits = 0;
            }
            return SystemVerilogVpiDescriptorError::None;
        }

        case SystemVerilogVpiDescriptorKind::ClassHandle:
            if (descriptor.category != SystemVerilogVpiValueCategory::None
                || descriptor.width != 64U || descriptor.is_signed
                || !valid_name(descriptor.nominal_name)
                || !descriptor.ranges.empty() || descriptor.maximum_size
                || !descriptor.children.empty() || !descriptor.member_names.empty()
                || !descriptor.enum_literals.empty()) {
                return SystemVerilogVpiDescriptorError::InvalidShape;
            }
            layout.fixed_element_count = 1;
            layout.fixed_bits = 64;
            layout.dynamic = true;
            return SystemVerilogVpiDescriptorError::None;
        }
        return SystemVerilogVpiDescriptorError::InvalidKind;
    }

} // namespace

SystemVerilogVpiDescriptorResult validate_systemverilog_vpi_descriptor(
    const SystemVerilogVpiTypeDescriptor& descriptor)
{
    SystemVerilogVpiDescriptorLayout layout;
    const auto error = measure(descriptor, 1, layout);
    if (error != SystemVerilogVpiDescriptorError::None) {
        return { { }, error };
    }
    if (layout.node_count > maximum_nodes) {
        return { { }, SystemVerilogVpiDescriptorError::NodeLimit };
    }
    return { layout, { } };
}

} // namespace fsim::runtime
