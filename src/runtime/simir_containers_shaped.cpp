// SPDX-License-Identifier: Apache-2.0
// VHDL arrays whose index ranges are known only at run time (IEEE 1076-2008
// 5.3.2.2): a container register holding one packed payload in the VHDL
// packed layout plus one index range per dimension.
#include "simir_internal.hpp"
#include "simir_container_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] std::uint64_t shape_length(const VhdlShapeRange& range) noexcept
{
    if (range.descending ? range.left < range.right
                         : range.left > range.right) {
        return 0U;
    }
    const auto distance = range.descending
        ? static_cast<std::uint64_t>(range.left)
            - static_cast<std::uint64_t>(range.right)
        : static_cast<std::uint64_t>(range.right)
            - static_cast<std::uint64_t>(range.left);
    return distance + 1U;
}

[[nodiscard]] PackedLogic4 signed_register_value(
    const std::int64_t value, const std::size_t width)
{
    const auto bits = static_cast<std::uint64_t>(value);
    if (width >= 64U) {
        PackedLogic4 result(width, Logic4::zero);
        result.insert_bits(PackedLogic4::from_aval_bval(64U, bits, 0U), 0U);
        if (value < 0) {
            for (std::size_t bit = 64U; bit < width; ++bit) {
                result.set(bit, Logic4::one);
            }
        }
        return result;
    }
    const auto mask = width == 0U ? 0U : (std::uint64_t { 1 } << width) - 1U;
    return PackedLogic4::from_aval_bval(width, bits & mask, 0U);
}

[[nodiscard]] std::int64_t known_index(
    const ProcessId process,
    const InstructionIndex instruction,
    const PackedLogic4& value)
{
    const auto known = value.known_signed_value();
    if (!known) {
        container_error(process, instruction,
            "a VHDL array index or bound is not a known integer");
    }
    return *known;
}

// The element ordinal of `indices` in row-major order, leftmost element
// first (IEEE 1076-2008 5.3.2.2).
[[nodiscard]] std::uint64_t shaped_ordinal(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value,
    const std::vector<std::int64_t>& indices)
{
    if (indices.size() != value.shape.size()) {
        container_error(process, instruction,
            "a VHDL array index has the wrong number of dimensions");
    }
    std::uint64_t ordinal { };
    for (std::size_t dimension { }; dimension < indices.size(); ++dimension) {
        const auto& range = value.shape[dimension];
        const auto index = indices[dimension];
        const auto low = range.descending ? range.right : range.left;
        const auto high = range.descending ? range.left : range.right;
        if (index < low || index > high) {
            container_error(process, instruction,
                "VHDL array index " + std::to_string(index)
                    + " is outside the index range " + std::to_string(range.left)
                    + (range.descending ? " downto " : " to ")
                    + std::to_string(range.right));
        }
        const auto position = range.descending
            ? static_cast<std::uint64_t>(range.left - index)
            : static_cast<std::uint64_t>(index - range.left);
        ordinal = ordinal * shape_length(range) + position;
    }
    return ordinal;
}

[[nodiscard]] std::size_t element_offset(
    const ContainerValue& value, const std::uint64_t ordinal)
{
    const auto width = value.type.element_width;
    const auto total = value.elements.front().width();
    return total - static_cast<std::size_t>(ordinal + 1U) * width;
}

// The bit offset and width addressed by an element access: the element
// selected by the leading indices and, with `inner`, a leaf inside it.
struct ShapedPlace {
    std::size_t offset { };
    std::size_t width { };
};

[[nodiscard]] ShapedPlace shaped_place(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value,
    const std::vector<std::int64_t>& indices,
    const std::vector<VhdlShapeRange>& inner,
    const std::uint32_t leaf_width)
{
    const auto outer_count = value.shape.size();
    if (indices.size() != outer_count + inner.size()) {
        container_error(process, instruction,
            "a VHDL array index has the wrong number of dimensions");
    }
    const std::vector<std::int64_t> outer(
        indices.begin(), indices.begin() + static_cast<std::ptrdiff_t>(outer_count));
    const auto ordinal = shaped_ordinal(process, instruction, value, outer);
    ShapedPlace place { element_offset(value, ordinal),
        value.type.element_width };
    if (inner.empty()) {
        return place;
    }
    ContainerValue element_shape;
    element_shape.shape = inner;
    const std::vector<std::int64_t> nested(
        indices.begin() + static_cast<std::ptrdiff_t>(outer_count),
        indices.end());
    const auto leaf = shaped_ordinal(process, instruction, element_shape, nested);
    if (leaf_width == 0U
        || (leaf + 1U) * leaf_width > value.type.element_width) {
        container_error(process, instruction,
            "a VHDL element access exceeds its element");
    }
    place.offset += value.type.element_width
        - static_cast<std::size_t>(leaf + 1U) * leaf_width;
    place.width = leaf_width;
    return place;
}

// The payload bits of a slice of a one-dimensional value (IEEE 1076-2008
// 8.5): a null slice is empty; otherwise its direction must be the
// prefix's and both bounds must lie in the index range.
[[nodiscard]] ShapedPlace slice_place(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value,
    const std::int64_t first,
    const std::int64_t last,
    const bool descending)
{
    if (value.shape.size() != 1U) {
        container_error(process, instruction,
            "a VHDL slice requires a one-dimensional array");
    }
    const VhdlShapeRange slice { first, last, descending };
    const auto length = shape_length(slice);
    if (length == 0U) {
        return { 0U, 0U };
    }
    const auto& range = value.shape.front();
    if (descending != range.descending) {
        container_error(process, instruction,
            "a VHDL slice direction differs from its prefix's index range");
    }
    const std::vector<std::int64_t> left_index { first };
    const std::vector<std::int64_t> right_index { last };
    const auto start = shaped_ordinal(process, instruction, value, left_index);
    static_cast<void>(
        shaped_ordinal(process, instruction, value, right_index));
    const auto width = value.type.element_width;
    const auto total = value.elements.front().width();
    return { total - static_cast<std::size_t>(start + length) * width,
        static_cast<std::size_t>(length) * width };
}

void require_shaped(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value)
{
    if (!value.type.vhdl_shaped || value.elements.size() != 1U
        || value.shape.size() != value.type.vhdl_rank) {
        container_error(process, instruction,
            "a VHDL shaped operation requires a shaped array value");
    }
}

} // namespace

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const VhdlShaped& operation)
{
    const auto id = process.id;
    const auto pc = process.pc;
    switch (operation.operation) {
    case VhdlShapedOperation::create: {
        // Without bounds the target is refilled in its own shape.
        const bool refill = operation.operands.empty()
            && operation.directions.empty();
        if ((!refill
                && (operation.operands.size()
                        != 2U * operation.directions.size()
                    || operation.directions.size() != operation.type.vhdl_rank))
            || operation.type.element_width == 0U) {
            container_error(id, pc, "malformed VHDL shaped value creation");
        }
        std::vector<VhdlShapeRange> shape;
        if (refill) {
            shape = get_container_register(process, operation.target).shape;
            if (shape.size() != operation.type.vhdl_rank) {
                container_error(id, pc, "malformed VHDL shaped value creation");
            }
        }
        ContainerValue value;
        value.type = operation.type;
        std::uint64_t count { 1U };
        for (std::size_t dimension { };
             dimension < operation.type.vhdl_rank; ++dimension) {
            VhdlShapeRange range;
            if (refill) {
                range = shape[dimension];
            } else {
                range.left = known_index(id, pc,
                    get_register(process, operation.operands[2U * dimension]));
                range.right = known_index(id, pc,
                    get_register(process,
                        operation.operands[2U * dimension + 1U]));
                range.descending = operation.directions[dimension] != 0U;
            }
            const auto length = shape_length(range);
            if (length != 0U
                && count > maximum_container_storage_bytes * 8U
                        / operation.type.element_width / length) {
                container_error(id, pc,
                    "a VHDL array value exceeds the storage budget");
            }
            count *= length;
            value.shape.push_back(range);
        }
        const auto& filler = get_register(process, operation.value);
        const auto width = operation.type.element_width;
        if (filler.width() != width) {
            container_error(id, pc,
                "a VHDL shaped value's element has the wrong width");
        }
        PackedLogic4 payload = filler.is_logic9()
            ? PackedLogic4::from_logic9_msb_string(
                  std::string(static_cast<std::size_t>(count) * width, 'U'))
            : PackedLogic4(static_cast<std::size_t>(count) * width,
                  Logic4::zero);
        for (std::uint64_t ordinal { }; ordinal < count; ++ordinal) {
            payload.insert_bits(filler,
                static_cast<std::size_t>(ordinal) * width);
        }
        value.elements.push_back(std::move(payload));
        get_container_register(process, operation.target) = std::move(value);
        break;
    }
    case VhdlShapedOperation::query: {
        const auto& source = read_container_register(process, operation.source);
        require_shaped(id, pc, source);
        if (operation.dimension >= source.shape.size()) {
            container_error(id, pc,
                "a VHDL array attribute names a missing dimension");
        }
        const auto& range = source.shape[operation.dimension];
        auto& destination = get_register(process, operation.value);
        const auto width = operation.width != 0U ? operation.width
            : destination.width() == 0U ? 64U : destination.width();
        std::int64_t result { };
        switch (operation.attribute) {
        case VhdlShapedAttribute::left:
            result = range.left;
            break;
        case VhdlShapedAttribute::right:
            result = range.right;
            break;
        case VhdlShapedAttribute::low:
            result = range.descending ? range.right : range.left;
            break;
        case VhdlShapedAttribute::high:
            result = range.descending ? range.left : range.right;
            break;
        case VhdlShapedAttribute::length:
            result = static_cast<std::int64_t>(shape_length(range));
            break;
        case VhdlShapedAttribute::ascending:
            result = range.descending ? 0 : 1;
            break;
        }
        destination = signed_register_value(result, width);
        break;
    }
    case VhdlShapedOperation::element_read: {
        const auto& source = read_container_register(process, operation.source);
        require_shaped(id, pc, source);
        std::vector<std::int64_t> indices;
        indices.reserve(operation.operands.size());
        for (const auto index : operation.operands) {
            indices.push_back(known_index(id, pc, get_register(process, index)));
        }
        const auto place = shaped_place(id, pc, source, indices,
            operation.inner, operation.width);
        get_register(process, operation.value)
            = source.elements.front().extract_bits(place.offset, place.width);
        break;
    }
    case VhdlShapedOperation::element_write: {
        std::vector<std::int64_t> indices;
        indices.reserve(operation.operands.size());
        for (const auto index : operation.operands) {
            indices.push_back(known_index(id, pc, get_register(process, index)));
        }
        const auto element = get_register(process, operation.value);
        auto& target = get_container_register(process, operation.target);
        require_shaped(id, pc, target);
        const auto place = shaped_place(id, pc, target, indices,
            operation.inner, operation.width);
        if (element.width() != place.width) {
            container_error(id, pc,
                "a VHDL shaped value's element has the wrong width");
        }
        target.elements.front().insert_bits(element, place.offset);
        break;
    }
    case VhdlShapedOperation::to_packed: {
        const auto& source = read_container_register(process, operation.source);
        require_shaped(id, pc, source);
        if (source.elements.front().width() != operation.width) {
            container_error(id, pc,
                "VHDL array length " + std::to_string(
                    source.elements.front().width() / source.type.element_width)
                    + " does not match its target's length");
        }
        get_register(process, operation.value) = source.elements.front();
        break;
    }
    case VhdlShapedOperation::from_packed: {
        const auto value = get_register(process, operation.value);
        auto& target = get_container_register(process, operation.target);
        require_shaped(id, pc, target);
        if (target.elements.front().width() != value.width()) {
            container_error(id, pc,
                "VHDL array value length does not match its target's length");
        }
        target.elements.front() = value;
        break;
    }
    case VhdlShapedOperation::slice_read:
    case VhdlShapedOperation::slice_write: {
        if (operation.operands.size() != 2U || operation.directions.size() != 1U) {
            container_error(id, pc, "malformed VHDL slice operation");
        }
        const auto first = known_index(id, pc,
            get_register(process, operation.operands[0]));
        const auto last = known_index(id, pc,
            get_register(process, operation.operands[1]));
        const bool descending = operation.directions.front() != 0U;
        if (operation.operation == VhdlShapedOperation::slice_read) {
            const auto& source
                = read_container_register(process, operation.source);
            require_shaped(id, pc, source);
            const auto place
                = slice_place(id, pc, source, first, last, descending);
            if (place.width != operation.width) {
                container_error(id, pc,
                    "VHDL slice length does not match its context's length");
            }
            get_register(process, operation.value) = place.width == 0U
                ? PackedLogic4(0U, Logic4::zero)
                : source.elements.front().extract_bits(
                      place.offset, place.width);
        } else {
            const auto value = get_register(process, operation.value);
            auto& target = get_container_register(process, operation.target);
            require_shaped(id, pc, target);
            const auto place
                = slice_place(id, pc, target, first, last, descending);
            if (place.width != value.width()) {
                container_error(id, pc,
                    "VHDL slice length does not match its value's length");
            }
            if (place.width != 0U) {
                target.elements.front().insert_bits(value, place.offset);
            }
        }
        break;
    }
    case VhdlShapedOperation::assign: {
        const auto source = read_container_register(process, operation.source);
        auto& target = get_container_register(process, operation.target);
        require_shaped(id, pc, source);
        require_shaped(id, pc, target);
        for (std::size_t dimension { }; dimension < target.shape.size();
             ++dimension) {
            if (shape_length(source.shape[dimension])
                != shape_length(target.shape[dimension])) {
                container_error(id, pc,
                    "VHDL array value length does not match its target's "
                    "length");
            }
        }
        target.elements.front() = source.elements.front();
        break;
    }
    case VhdlShapedOperation::equal:
    case VhdlShapedOperation::equal_packed: {
        const auto& source = read_container_register(process, operation.source);
        require_shaped(id, pc, source);
        bool same { };
        if (operation.operation == VhdlShapedOperation::equal) {
            const auto& other = read_container_register(process, operation.target);
            require_shaped(id, pc, other);
            same = source.shape.size() == other.shape.size()
                && source.elements.front().width()
                    == other.elements.front().width();
            for (std::size_t dimension { }; same && dimension < source.shape.size();
                 ++dimension) {
                same = shape_length(source.shape[dimension])
                    == shape_length(other.shape[dimension]);
            }
            same = same
                && (source.elements.front().width() == 0U
                    || source.elements.front() == other.elements.front());
        } else {
            if (operation.operands.size() != 1U) {
                container_error(id, pc, "malformed VHDL array comparison");
            }
            const auto& value = get_register(process, operation.operands.front());
            same = source.elements.front().width() == value.width()
                && (value.width() == 0U || source.elements.front() == value);
        }
        get_register(process, operation.value)
            = PackedLogic4::from_aval_bval(1U, same ? 1U : 0U, 0U);
        break;
    }
    case VhdlShapedOperation::append:
    case VhdlShapedOperation::append_packed: {
        auto& target = get_container_register(process, operation.target);
        require_shaped(id, pc, target);
        if (target.shape.size() != 1U) {
            container_error(id, pc,
                "VHDL concatenation requires one-dimensional operands");
        }
        PackedLogic4 right;
        std::optional<VhdlShapeRange> right_range;
        if (operation.operation == VhdlShapedOperation::append) {
            const auto& source = read_container_register(process, operation.source);
            require_shaped(id, pc, source);
            if (source.shape.size() != 1U) {
                container_error(id, pc,
                    "VHDL concatenation requires one-dimensional operands");
            }
            right = source.elements.front();
            right_range = source.shape.front();
        } else {
            if (operation.operands.size() != 1U) {
                container_error(id, pc, "malformed VHDL concatenation");
            }
            right = get_register(process, operation.operands.front());
        }
        const auto element_width = target.type.element_width;
        if (right.width() % element_width != 0U) {
            container_error(id, pc,
                "a VHDL concatenation operand has a partial element");
        }
        auto& left = target.elements.front();
        auto& range = target.shape.front();
        // A null left operand yields the right one (9.2.5).
        if (shape_length(range) == 0U && right_range) {
            range = *right_range;
            left = right;
            break;
        }
        const auto count = static_cast<std::int64_t>(
            (left.width() + right.width()) / element_width);
        const auto total = left.width() + right.width();
        PackedLogic4 joined = left.is_logic9() || right.is_logic9()
            ? PackedLogic4::from_logic9_msb_string(std::string(total, 'U'))
            : PackedLogic4(total, Logic4::zero);
        if (right.width() != 0U) {
            joined.insert_bits(right, 0U);
        }
        if (left.width() != 0U) {
            joined.insert_bits(left, right.width());
        }
        left = std::move(joined);
        range.right = range.descending ? range.left - (count - 1)
                                       : range.left + (count - 1);
        break;
    }
    }
    ++process.pc;
}

} // namespace fsim::runtime::simir
