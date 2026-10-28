// SPDX-License-Identifier: Apache-2.0
#include "simir_container_ops.hpp"
#include "simir_internal.hpp"
#include "simir_container_helpers.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace fsim::runtime::simir {
namespace {

// Follow an aggregate member path from one aggregate value.
template <typename Value>
[[nodiscard]] Value* follow_members(
    const ProcessId process,
    const InstructionIndex instruction,
    Value* selected,
    const std::vector<std::uint32_t>& members)
{
    for (const auto member : members) {
        if (!aggregate_box(selected->type)
            || member >= selected->nested_elements.size()) {
            container_error(
                process, instruction, "aggregate member path is invalid");
        }
        selected = &selected->nested_elements[member];
    }
    return selected;
}

void append_container_bits(
    const ProcessId process,
    const InstructionIndex instruction,
    std::vector<PackedLogic4>& bits,
    const ContainerValue& value)
{
    const auto append_packed = [&](const PackedLogic4& element,
                                   const std::size_t width) {
        for (std::size_t bit = width; bit-- > 0U;) {
            PackedLogic4 single(1U, Logic4::zero);
            single.set(0U, bit < element.width() ? element.get(bit)
                                                 : Logic4::zero);
            bits.push_back(std::move(single));
        }
    };
    if (aggregate_box(value.type)) {
        for (const auto& member : value.nested_elements) {
            append_container_bits(process, instruction, bits, member);
        }
        return;
    }
    switch (value.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        for (const auto& element : value.elements) {
            append_packed(element, value.type.element_width);
        }
        return;
    case ContainerElementKind::String:
        for (const auto& text : value.string_elements) {
            for (const auto character : text) {
                append_packed(PackedLogic4::from_aval_bval(8U,
                    static_cast<unsigned char>(character), 0U), 8U);
            }
        }
        return;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        for (const auto& element : value.nested_elements) {
            append_container_bits(process, instruction, bits, element);
        }
        return;
    }
    container_error(process, instruction, "bit-stream source is invalid");
}

// Consume bits into a container value's elements; a variable-size
// container takes as many elements as the remaining bits fill, less the
// `reserved` bits of the fixed-size targets after it, the last one padded
// with zero bits (IEEE 1800-2017 11.4.14.4).
void consume_container_bits(
    const ProcessId process,
    const InstructionIndex instruction,
    const std::vector<PackedLogic4>& bits,
    std::size_t& position,
    ContainerValue& value,
    const bool variable_size,
    const std::size_t reserved = 0U)
{
    // A 2-state element converts X and Z bits to 0 (IEEE 1800-2017 6.22).
    const bool two_state = value.type.two_state
        || value.type.element_kind == ContainerElementKind::Scalar;
    const auto take_packed = [&](const std::size_t width) {
        PackedLogic4 element(width, Logic4::zero);
        for (std::size_t bit = width; bit-- > 0U;) {
            if (position < bits.size()) {
                const auto selected = bits[position].get(0U);
                element.set(bit, two_state && selected != Logic4::one
                        ? Logic4::zero
                        : selected);
                ++position;
            }
        }
        return element;
    };
    if (aggregate_box(value.type)) {
        for (auto& member : value.nested_elements) {
            consume_container_bits(
                process, instruction, bits, position, member, false);
        }
        return;
    }
    const bool resize = variable_size && !value.type.fixed
        && !value.type.associative;
    const auto element_bits = [&]() -> std::size_t {
        switch (value.type.element_kind) {
        case ContainerElementKind::Packed:
        case ContainerElementKind::Scalar:
            return value.type.element_width;
        case ContainerElementKind::String:
            return 8U;
        default:
            return 0U;
        }
    }();
    if (resize) {
        if (element_bits == 0U) {
            container_error(process, instruction,
                "a variable-size bit-stream target needs packed elements");
        }
        const auto available = bits.size() - std::min(position, bits.size());
        const auto remaining = available - std::min(reserved, available);
        const auto count = (remaining + element_bits - 1U) / element_bits;
        resize_container_value(value, count);
    }
    switch (value.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        for (auto& element : value.elements) {
            element = take_packed(value.type.element_width);
        }
        return;
    case ContainerElementKind::String:
        for (auto& text : value.string_elements) {
            const auto code = take_packed(8U).known_unsigned_value();
            text.assign(1U, static_cast<char>(code.value_or(0U)));
        }
        return;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        for (auto& element : value.nested_elements) {
            consume_container_bits(
                process, instruction, bits, position, element, false);
        }
        return;
    }
}

} // namespace

ContainerValue* follow_container_members(
    const ProcessId process,
    const InstructionIndex instruction,
    ContainerValue* const selected,
    const std::vector<std::uint32_t>& members)
{
    return follow_members(process, instruction, selected, members);
}

const ContainerValue* follow_container_members(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue* const selected,
    const std::vector<std::uint32_t>& members)
{
    return follow_members(process, instruction, selected, members);
}

ContainerValue* select_container_element_for_write(
    ContainerValue& target,
    const PackedLogic4& index,
    const bool signed_index,
    const bool linear_index,
    const ProcessId process,
    const InstructionIndex instruction,
    const std::string* string_key)
{
    if (aggregate_box(target.type)) {
        return &target;
    }
    const auto element_type = nested_element_container_type(target.type);
    if (!element_type) {
        container_error(
            process, instruction, "nested element write profile mismatch");
    }
    std::size_t at { };
    if (target.type.associative && string_key != nullptr) {
        const auto& key = associative_string_key(
            process, instruction, target.type, *string_key);
        at = lower_string_key(target, key);
        if (at >= target.string_keys.size() || target.string_keys[at] != key) {
            if (target.string_keys.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process, instruction,
                    "associative nested container exceeds its owning-storage budget");
            }
            target.string_keys.insert(iterator_at(target.string_keys, at), key);
            target.nested_elements.insert(
                iterator_at(target.nested_elements, at),
                default_container_value(*element_type));
        }
    } else if (target.type.associative) {
        const auto key = associative_key(
            process, instruction, target.type, index);
        at = lower_key(target, key);
        if (at >= target.keys.size() || !key_equal(target.keys[at], key)) {
            if (target.keys.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process, instruction,
                    "associative nested container exceeds its owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.nested_elements.insert(
                iterator_at(target.nested_elements, at),
                default_container_value(*element_type));
        }
    } else if (target.type.fixed && linear_index) {
        at = known_index(
            process, instruction, index, true,
            "multidimensional linear index");
        if (at >= target.nested_elements.size()) {
            container_error(
                process, instruction,
                "multidimensional linear index is out of range");
        }
    } else if (target.type.fixed
        && !fixed_index_in_range(target.type, index)) {
        // A write through an unknown or out-of-range index writes nothing
        // (IEEE 1800-2017 7.4.6).
        return nullptr;
    } else if (!target.type.fixed) {
        // A queue or dynamic array ignores a write through an unknown,
        // negative or out-of-range index; a queue write one past its last
        // element appends (IEEE 1800-2017 7.4.6, 7.10.1).
        std::optional<std::int64_t> requested;
        if (signed_index) {
            requested = index.known_signed_value();
        } else if (const auto value = index.known_unsigned_value();
                   value
                   && *value <= static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max())) {
            requested = static_cast<std::int64_t>(*value);
        }
        const auto size = target.nested_elements.size();
        if (!requested || *requested < 0
            || static_cast<std::uint64_t>(*requested) > size
            || (static_cast<std::uint64_t>(*requested) == size
                && (!target.type.queue
                    || (target.type.maximum_elements
                        && size >= *target.type.maximum_elements)))) {
            return nullptr;
        }
        if (static_cast<std::uint64_t>(*requested) == size) {
            target.nested_elements.push_back(
                default_container_value(*element_type));
        }
        at = static_cast<std::size_t>(*requested);
    } else {
        at = fixed_offset(process, instruction, target.type, index);
        if (at >= target.nested_elements.size()) {
            container_error(
                process, instruction,
                "nested container index is out of range");
        }
    }
    return &target.nested_elements[at];
}

void apply_container_aggregate_member(
    ContainerValue& target,
    const PackedLogic4& index,
    const bool signed_index,
    const bool linear_index,
    const std::span<const std::uint32_t> members,
    const PackedLogic4& value,
    const ProcessId process,
    const InstructionIndex instruction)
{
    if (target.type.element_kind != ContainerElementKind::Aggregate
        || members.empty()) {
        container_error(
            process, instruction,
            "aggregate member write requires an unpacked aggregate container");
    }
    auto* selected = aggregate_box(target.type)
        ? &target
        : static_cast<ContainerValue*>(nullptr);
    std::size_t at { };
    if (selected != nullptr) {
        // An unpacked aggregate object is itself the selected value. Arrays
        // of unpacked aggregates select an element before walking members.
    } else if (target.type.associative) {
        const auto key = associative_key(
            process, instruction, target.type,
            index);
        at = lower_key(target, key);
        if (at >= target.keys.size()
            || !key_equal(target.keys[at], key)) {
            if (target.keys.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process, instruction,
                    "associative aggregate exceeds its owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.nested_elements.insert(
                iterator_at(target.nested_elements, at),
                default_container_value(
                    aggregate_element_type(target.type)));
        }
    } else {
        at = target.type.fixed
            ? linear_index
                ? known_index(
                      process, instruction,
                      index, true,
                      "multidimensional linear index")
                : fixed_offset(
                      process, instruction, target.type,
                      index)
            : known_index(
                  process, instruction,
                  index,
                  signed_index, "container index");
        if (at >= target.nested_elements.size()) {
            container_error(
                process, instruction,
                "aggregate container index is out of range");
        }
    }
    if (selected == nullptr) {
        selected = &target.nested_elements[at];
    }
    ContainerValue* direct_union = nullptr;
    for (std::size_t path_index = 0;
        path_index < members.size(); ++path_index) {
        const auto member = members[path_index];
        if (selected->type.element_kind != ContainerElementKind::Aggregate
            || member >= selected->nested_elements.size()) {
            container_error(
                process, instruction,
                "aggregate member write path is invalid");
        }
        if (selected->type.union_aggregate
            && path_index + 1U == members.size()) {
            direct_union = selected;
        }
        selected = &selected->nested_elements[member];
    }
    if (!selected->type.fixed || selected->elements.size() != 1U
        || value.width() != selected->type.element_width
        || value.is_logic9()
        || (selected->type.two_state && has_unknown(value))) {
        container_error(
            process, instruction,
            "aggregate member write leaf type mismatch");
    }
    selected->elements.front() = value;
    if (direct_union != nullptr) {
        for (auto& sibling : direct_union->nested_elements) {
            if (sibling.type == selected->type && sibling.type.fixed
                && sibling.elements.size() == 1U) {
                sibling.elements.front() = value;
            }
        }
    }
}

void execute_container_operation(
    const ResizeContainer& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    if ((target.type.queue && !operation.allow_queue)
        || target.type.associative || target.type.fixed) {
        container_error(
            process, instruction,
            target.type.queue
                ? "new[size] cannot resize a queue"
                : target.type.associative
                ? "new[size] cannot resize an associative array"
                : "new[size] cannot resize a static array");
    }
    const auto size = known_index(
        process, instruction,
        access.read(operation.size),
        false, "dynamic-array size");
    const ContainerValue* initializer { };
    if (operation.initializer) {
        const auto& source = access.read_container(*operation.initializer);
        if (!new_initializer_compatible(source.type, target.type)) {
            container_error(
                process, instruction,
                "dynamic-array initializer type mismatch");
        }
        initializer = &source;
    }
    try {
        resize_container_value(target, size, initializer);
    } catch (const std::exception& error) {
        container_error(
            process, instruction, error.what());
    }
}

void execute_container_operation(
    const CopyContainerRegister& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& destination = access.container(operation.destination);
    const auto& source = access.read_container(operation.source);
    require_same_type(
        process, instruction,
        destination.type, source.type);
    destination = source;
}

void execute_container_operation(
    const ConditionalContainerSelect& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& destination = access.container(operation.destination);
    const auto& when_true = access.read_container(operation.when_true);
    const auto& when_false = access.read_container(operation.when_false);
    const auto& condition = access.read(operation.condition);
    try {
        select_container_value(
            destination, condition, when_true, when_false);
    } catch (const std::exception& error) {
        container_error(
            process, instruction, error.what());
    }
}

void execute_container_operation(
    const CompareContainers& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    try {
        access.write(operation.destination, compare_container_values(
            access.read_container(operation.lhs),
            access.read_container(operation.rhs),
            operation.case_equal));
    } catch (const std::exception& error) {
        container_error(
            process, instruction, error.what());
    }
}

void execute_container_operation(
    const ContainerSize& operation,
    ContainerRegisterAccess& access,
    const ProcessId /*process*/,
    const InstructionIndex /*instruction*/)
{
    const auto size = container_value_size(access.read_container(operation.source));
    access.write(operation.destination, PackedLogic4::from_aval_bval(32, size, 0));
}

void execute_container_operation(
    const ContainerReduction& operation,
    ContainerRegisterAccess& access,
    const ProcessId /*process*/,
    const InstructionIndex /*instruction*/)
{
    access.write(operation.destination, reduce_container_value(
        access.read_container(operation.source),
        operation.operation, operation.transformation));
}

void execute_container_operation(
    const OrderContainer& operation,
    ContainerRegisterAccess& access,
    const ProcessId /*process*/,
    const InstructionIndex /*instruction*/)
{
    auto& target = access.container(operation.target);
    order_container_value(
        target, operation.operation, operation.key,
        [&]() { return access.next_random(); });
}

void execute_container_operation(
    const LocateContainer& operation,
    ContainerRegisterAccess& access,
    const ProcessId /*process*/,
    const InstructionIndex /*instruction*/)
{
    auto& destination = access.container(operation.destination);
    const auto& source = access.read_container(operation.source);
    locate_container_values(
        destination, source, operation.operation,
        operation.predicate, operation.transformation);
}

void execute_container_operation(
    const ContainerRead& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    const auto& source = access.read_container(operation.source);
    if (source.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process, instruction, source.type,
                access.read_string(operation.index));
            const auto at = lower_string_key(source, key);
            access.write(operation.destination, at < source.string_keys.size()
                    && source.string_keys[at] == key
                ? source.elements[at]
                : source.associative_default.value_or(
                    default_container_element(source.type)));
            return;
        }
        const auto key = associative_key(
            process, instruction, source.type,
            access.read(operation.index));
        const auto at = lower_key(source, key);
        access.write(operation.destination,
            at < source.keys.size() && key_equal(source.keys[at], key)
            ? source.elements[at]
            : source.associative_default.value_or(
                    default_container_element(source.type)));
        return;
    }
    if (source.type.fixed) {
        const auto& index_value = access.read(operation.index);
        if (source.type.sparse) {
            const auto index = index_value.known_signed_value();
            const auto low = std::min(
                source.type.index_left, source.type.index_right);
            const auto high = std::max(
                source.type.index_left, source.type.index_right);
            const auto ordinal = !index ? std::nullopt
                : operation.linear_index
                ? *index >= 0 ? std::optional { static_cast<std::uint64_t>(*index) }
                              : std::nullopt
                : *index >= low && *index <= high
                ? std::optional { sparse_offset(process, instruction,
                      source.type, index_value) }
                : std::nullopt;
            access.write(operation.destination, ordinal
                    && *ordinal < sparse_container_element_count(source.type)
                ? read_sparse_container_element(source, *ordinal)
                : PackedLogic4(source.type.element_width,
                      source.type.two_state ? Logic4::zero : Logic4::x));
            return;
        }
        if (operation.linear_index) {
            const auto index = index_value.known_signed_value();
            if (!index || *index < 0
                || static_cast<std::uint64_t>(*index)
                    >= source.elements.size()) {
                access.write(operation.destination, PackedLogic4(
                    source.type.element_width,
                    source.type.two_state
                        ? Logic4::zero
                        : Logic4::x));
                return;
            }
            access.write(operation.destination,
                source.elements[static_cast<std::size_t>(*index)]);
            return;
        }
        const auto index = index_value.known_signed_value();
        const auto low = std::min(
            source.type.index_left, source.type.index_right);
        const auto high = std::max(
            source.type.index_left, source.type.index_right);
        if (!index || *index < low || *index > high) {
            access.write(operation.destination, PackedLogic4(
                source.type.element_width,
                source.type.two_state
                    ? Logic4::zero
                    : Logic4::x));
            return;
        }
        access.write(operation.destination,
            source.elements[fixed_offset(
                source.type, static_cast<std::int32_t>(*index))]);
        return;
    }
    const auto index = known_index(
        process, instruction,
        access.read(operation.index),
        operation.signed_index, "container index");
    if (index >= source.elements.size()) {
        container_error(
            process, instruction,
            "container index is out of range");
    }
    access.write(operation.destination, source.elements[index]);
}

void execute_container_operation(
    const ContainerWrite& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    const auto& source = access.read(operation.source);
    if (source.width() != target.type.element_width
        || source.is_logic9()
        || (target.type.two_state && has_unknown(source))) {
        container_error(
            process, instruction,
            "container element write type mismatch");
    }
    if (operation.associative_default) {
        if (!target.type.associative) {
            container_error(process, instruction,
                "a container default requires an associative array");
        }
        target.associative_default = source;
        return;
    }
    if (target.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process, instruction, target.type,
                access.read_string(operation.index));
            const auto at = lower_string_key(target, key);
            if (at < target.string_keys.size()
                && target.string_keys[at] == key) {
                target.elements[at] = source;
            } else {
                if (target.elements.size()
                    >= maximum_container_elements(target.type)) {
                    container_error(
                        process, instruction,
                        "associative array exceeds the per-container "
                        "owning-storage budget");
                }
                target.string_keys.insert(
                    iterator_at(target.string_keys, at), key);
                target.elements.insert(
                    iterator_at(target.elements, at), source);
            }
            validate_container_value(target);
            return;
        }
        const auto key = associative_key(
            process, instruction, target.type,
            access.read(operation.index));
        const auto at = lower_key(target, key);
        if (at < target.keys.size()
            && key_equal(target.keys[at], key)) {
            target.elements[at] = source;
        } else {
            if (target.elements.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process, instruction,
                    "associative array exceeds the per-container "
                    "owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.elements.insert(iterator_at(target.elements, at), source);
        }
        return;
    }
    if (target.type.sparse) {
        const auto ordinal = operation.linear_index
            ? known_index(
                  process, instruction,
                  access.read(operation.index),
                  true, "multidimensional linear index")
            : sparse_offset(process, instruction, target.type,
                  access.read(operation.index));
        if (ordinal >= sparse_container_element_count(target.type)) {
            container_error(
                process, instruction,
                "multidimensional linear index is out of range");
        }
        static_cast<void>(
            write_sparse_container_element(target, ordinal, source));
        return;
    }
    if (target.type.fixed) {
        if (operation.linear_index) {
            const auto index = known_index(
                process, instruction,
                access.read(operation.index),
                true, "multidimensional linear index");
            if (index >= target.elements.size()) {
                container_error(
                    process, instruction,
                    "multidimensional linear index is out of range");
            }
            target.elements[index] = source;
            return;
        }
        target.elements[fixed_offset(
            process, instruction, target.type,
            access.read(operation.index))] = source;
        return;
    }
    // Local sequence registers follow the same indexed-write rules as
    // container objects: invalid writes do nothing, and writing the next
    // queue index appends one element (IEEE 1800-2017 7.4.6, 7.10.1).
    const auto& index_value = access.read(operation.index);
    const auto requested = operation.signed_index
        ? index_value.known_signed_value()
        : [&]() -> std::optional<std::int64_t> {
              const auto value = index_value.known_unsigned_value();
              if (!value
                  || *value > static_cast<std::uint64_t>(
                         std::numeric_limits<std::int64_t>::max())) {
                  return std::nullopt;
              }
              return static_cast<std::int64_t>(*value);
          }();
    const auto size = target.elements.size();
    if (!requested || *requested < 0
        || static_cast<std::uint64_t>(*requested) > size
        || (static_cast<std::uint64_t>(*requested) == size
            && (!target.type.queue
                || (target.type.maximum_elements
                    && size >= *target.type.maximum_elements)))) {
        return;
    }
    if (static_cast<std::uint64_t>(*requested) == size) {
        if (size >= maximum_container_elements(target.type)) {
            container_error(process, instruction,
                "queue exceeds the per-container owning-storage budget");
        }
        target.elements.push_back(source);
    } else {
        target.elements[static_cast<std::size_t>(*requested)] = source;
    }
}

void execute_container_operation(
    const ContainerStringRead& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    const auto& source = access.read_container(operation.source);
    if (!operation.members.empty()) {
        if (source.type.element_kind != ContainerElementKind::Aggregate
            || source.type.associative || operation.string_index) {
            container_error(
                process, instruction,
                "aggregate string member read requires an indexed aggregate container");
        }
        // A structure value is its own element.
        const auto at = source.type.aggregate_value ? std::size_t { }
            : source.type.fixed
            ? operation.linear_index
                ? known_index(
                      process, instruction,
                      access.read(operation.index), true,
                      "multidimensional linear index")
                : fixed_offset(
                      process, instruction, source.type,
                      access.read(operation.index))
            : known_index(
                  process, instruction,
                  access.read(operation.index),
                  operation.signed_index, "container index");
        if (!source.type.aggregate_value
            && at >= source.nested_elements.size()) {
            container_error(
                process, instruction,
                "aggregate container index is out of range");
        }
        const auto* selected = source.type.aggregate_value
            ? &source
            : &source.nested_elements[at];
        for (const auto member : operation.members) {
            if (selected->type.element_kind != ContainerElementKind::Aggregate
                || member >= selected->nested_elements.size()) {
                container_error(
                    process, instruction,
                    "aggregate string member read path is invalid");
            }
            selected = &selected->nested_elements[member];
        }
        if (selected->type.element_kind != ContainerElementKind::String
            || !selected->type.fixed
            || selected->string_elements.size() != 1U) {
            container_error(
                process, instruction,
                "aggregate string member read requires a scalar string leaf");
        }
        access.string(operation.destination)
            = selected->string_elements.front();
        return;
    }
    if (source.type.element_kind != ContainerElementKind::String) {
        container_error(
            process, instruction,
            "string element read requires a string container");
    }
    if (source.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process, instruction, source.type,
                access.read_string(operation.index));
            const auto at = lower_string_key(source, key);
            access.string(operation.destination) = at < source.string_keys.size()
                    && source.string_keys[at] == key
                ? source.string_elements[at]
                : source.associative_default_text.value_or(std::string { });
            return;
        }
        const auto key = associative_key(
            process, instruction, source.type,
            access.read(operation.index));
        const auto at = lower_key(source, key);
        access.string(operation.destination) = at < source.keys.size() && key_equal(source.keys[at], key)
            ? source.string_elements[at]
            : source.associative_default_text.value_or(std::string { });
        return;
    }
    // Reading an invalid index yields the empty string (IEEE 1800-2017
    // 7.4.6): an unknown or negative index of a queue or dynamic array too.
    if (!source.type.fixed) {
        const auto& raw = access.read(operation.index);
        std::optional<std::int64_t> requested;
        if (operation.signed_index) {
            requested = raw.known_signed_value();
        } else if (const auto value = raw.known_unsigned_value();
                   value
                   && *value <= static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max())) {
            requested = static_cast<std::int64_t>(*value);
        }
        if (!requested || *requested < 0) {
            access.string(operation.destination)
                = std::string { };
            return;
        }
    }
    const auto at = source.type.fixed
        ? operation.linear_index
            ? known_index(
                  process, instruction,
                  access.read(operation.index), true,
                  "multidimensional linear index")
            : fixed_offset(
                  process, instruction, source.type,
                  access.read(operation.index))
        : known_index(
              process, instruction,
              access.read(operation.index),
              operation.signed_index, "container index");
    access.string(operation.destination)
        = at < source.string_elements.size()
        ? source.string_elements[at]
        : std::string { };
}

void execute_container_operation(
    const ContainerStringWrite& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    if (operation.associative_default) {
        if (!target.type.associative) {
            container_error(process, instruction,
                "a container default requires an associative array");
        }
        target.associative_default_text
            = access.read_string(operation.source);
        return;
    }
    if (!operation.members.empty()) {
        if (target.type.element_kind != ContainerElementKind::Aggregate
            || target.type.associative || operation.string_index) {
            container_error(
                process, instruction,
                "aggregate string member write requires an indexed aggregate container");
        }
        const auto at = target.type.aggregate_value ? std::size_t { }
            : target.type.fixed
            ? operation.linear_index
                ? known_index(
                      process, instruction,
                      access.read(operation.index), true,
                      "multidimensional linear index")
                : fixed_offset(
                      process, instruction, target.type,
                      access.read(operation.index))
            : known_index(
                  process, instruction,
                  access.read(operation.index),
                  operation.signed_index, "container index");
        if (!target.type.aggregate_value
            && at >= target.nested_elements.size()) {
            container_error(
                process, instruction,
                "aggregate container index is out of range");
        }
        auto* selected = target.type.aggregate_value
            ? &target
            : &target.nested_elements[at];
        for (const auto member : operation.members) {
            if (selected->type.element_kind != ContainerElementKind::Aggregate
                || member >= selected->nested_elements.size()) {
                container_error(
                    process, instruction,
                    "aggregate string member write path is invalid");
            }
            selected = &selected->nested_elements[member];
        }
        if (selected->type.element_kind != ContainerElementKind::String
            || !selected->type.fixed
            || selected->string_elements.size() != 1U) {
            container_error(
                process, instruction,
                "aggregate string member write requires a scalar string leaf");
        }
        selected->string_elements.front()
            = access.read_string(operation.source);
        return;
    }
    if (target.type.element_kind != ContainerElementKind::String) {
        container_error(
            process, instruction,
            "string element write requires a string container");
    }
    const auto source = access.read_string(operation.source);
    if (target.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process, instruction, target.type,
                access.read_string(operation.index));
            const auto at = lower_string_key(target, key);
            if (at < target.string_keys.size()
                && target.string_keys[at] == key) {
                target.string_elements[at] = source;
            } else {
                if (target.string_keys.size()
                    >= maximum_container_elements(target.type)) {
                    container_error(
                        process, instruction,
                        "associative string array exceeds its owning-storage budget");
                }
                target.string_keys.insert(
                    iterator_at(target.string_keys, at), key);
                target.string_elements.insert(
                    iterator_at(target.string_elements, at), source);
            }
            validate_container_value(target);
            return;
        }
        const auto key = associative_key(
            process, instruction, target.type,
            access.read(operation.index));
        const auto at = lower_key(target, key);
        if (at < target.keys.size() && key_equal(target.keys[at], key)) {
            target.string_elements[at] = source;
        } else {
            if (target.keys.size() >= maximum_container_elements(target.type)) {
                container_error(
                    process, instruction,
                    "associative string array exceeds its owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.string_elements.insert(
                iterator_at(target.string_elements, at), source);
        }
        return;
    }
    // A queue or dynamic array ignores a write through an unknown,
    // negative or out-of-range index; a queue write one past its last
    // element appends (IEEE 1800-2017 7.4.6, 7.10.1).
    if (!target.type.fixed && !operation.linear_index) {
        const auto& raw = access.read(operation.index);
        std::optional<std::int64_t> requested;
        if (operation.signed_index) {
            requested = raw.known_signed_value();
        } else if (const auto value = raw.known_unsigned_value();
                   value
                   && *value <= static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max())) {
            requested = static_cast<std::int64_t>(*value);
        }
        const auto size = target.string_elements.size();
        if (!requested || *requested < 0
            || static_cast<std::uint64_t>(*requested) > size
            || (static_cast<std::uint64_t>(*requested) == size
                && (!target.type.queue
                    || (target.type.maximum_elements
                        && size >= *target.type.maximum_elements)))) {
            return;
        }
        if (static_cast<std::uint64_t>(*requested) == size) {
            target.string_elements.push_back(source);
            return;
        }
    }
    const auto at = target.type.fixed
        ? operation.linear_index
            ? known_index(
                  process, instruction,
                  access.read(operation.index), true,
                  "multidimensional linear index")
            : fixed_offset(
                  process, instruction, target.type,
                  access.read(operation.index))
        : known_index(
              process, instruction,
              access.read(operation.index),
              operation.signed_index, "container index");
    if (at >= target.string_elements.size()) {
        container_error(
            process, instruction,
            "string container index is out of range");
    }
    target.string_elements[at] = source;
}

void execute_container_operation(
    const FormatContainerPattern& operation,
    ContainerRegisterAccess& access,
    const ProcessId /*process*/,
    const InstructionIndex /*instruction*/)
{
    access.string(operation.destination)
        = format_container_assignment_pattern(
            access.read_container(operation.source));
}

void execute_container_operation(
    const ContainerElementRead& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    const auto& source = access.read_container(operation.source);
    const ContainerValue* selected = nullptr;
    std::optional<ContainerValue> missing;
    if (aggregate_box(source.type)) {
        selected = &source;
    } else {
        const auto element_type = nested_element_container_type(source.type);
        if (!element_type) {
            container_error(
                process, instruction,
                "nested element read requires a nested container");
        }
        if (source.type.associative && operation.string_index) {
            const auto& key = associative_string_key(
                process, instruction, source.type,
                access.read_string(operation.index));
            const auto at = lower_string_key(source, key);
            if (at < source.string_keys.size()
                && source.string_keys[at] == key) {
                selected = &source.nested_elements[at];
            }
        } else {
            const auto& index = access.read(operation.index);
            if (source.type.associative) {
                const auto key = associative_key(
                    process, instruction, source.type, index);
                const auto at = lower_key(source, key);
                if (at < source.keys.size()
                    && key_equal(source.keys[at], key)) {
                    selected = &source.nested_elements[at];
                }
            } else if (source.type.fixed && operation.linear_index) {
                const auto at = known_index(
                    process, instruction, index, true,
                    "multidimensional linear index");
                if (at < source.nested_elements.size()) {
                    selected = &source.nested_elements[at];
                }
            } else if (source.type.fixed) {
                if (fixed_index_in_range(source.type, index)) {
                    const auto at = fixed_offset(
                        process, instruction, source.type, index);
                    if (at < source.nested_elements.size()) {
                        selected = &source.nested_elements[at];
                    }
                }
            } else {
                // A negative index of a queue or dynamic array selects no
                // element (IEEE 1800-2017 7.4.6).
                std::optional<std::int64_t> requested;
                if (operation.signed_index) {
                    requested = index.known_signed_value();
                } else if (const auto value = index.known_unsigned_value();
                           value
                           && *value <= static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int64_t>::max())) {
                    requested = static_cast<std::int64_t>(*value);
                }
                if (requested && *requested >= 0
                    && static_cast<std::uint64_t>(*requested)
                        < source.nested_elements.size()) {
                    selected = &source.nested_elements[
                        static_cast<std::size_t>(*requested)];
                }
            }
        }
        if (selected == nullptr) {
            // A read through an unknown, absent or out-of-range index yields
            // the element type's default (IEEE 1800-2017 7.4.6).
            missing = default_container_value(*element_type);
            selected = &*missing;
        }
    }
    selected = follow_container_members(
        process, instruction, selected, operation.members);
    auto& destination = access.container(operation.destination);
    if (destination.type != selected->type) {
        container_error(
            process, instruction,
            "nested element read profile mismatch");
    }
    destination = *selected;
}

void execute_container_operation(
    const ContainerElementWrite& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    const auto& source = access.read_container(operation.source);
    auto* selected = operation.string_index
        ? select_container_element_for_write(target, PackedLogic4 { },
              false, false, process, instruction,
              &access.read_string(operation.index))
        : select_container_element_for_write(
              target, access.read(operation.index),
              operation.signed_index, operation.linear_index, process,
              instruction);
    if (selected == nullptr) {
        return;
    }
    selected = follow_container_members(
        process, instruction, selected, operation.members);
    if (selected->type != source.type) {
        container_error(
            process, instruction,
            "nested element write profile mismatch");
    }
    *selected = source;
}

void execute_container_operation(
    const ContainerAggregateRead& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    const auto& source = access.read_container(operation.source);
    if (source.type.element_kind != ContainerElementKind::Aggregate
        || operation.members.empty()) {
        container_error(
            process, instruction,
            "aggregate member read requires an unpacked aggregate container");
    }
    std::optional<ContainerValue> missing;
    const ContainerValue* selected = nullptr;
    if (aggregate_box(source.type)) {
        selected = &source;
    } else if (source.type.associative) {
        const auto key = associative_key(
            process, instruction, source.type,
            access.read(operation.index));
        const auto at = lower_key(source, key);
        if (at < source.keys.size()
            && key_equal(source.keys[at], key)) {
            selected = &source.nested_elements[at];
        } else {
            missing = default_container_value(
                aggregate_element_type(source.type));
            selected = &*missing;
        }
    } else if (const auto& index = access.read(operation.index);
               std::ranges::any_of(index.bval_words(),
                   [](const auto word) { return word != 0U; })
               || (source.type.fixed && !operation.linear_index
                   && !fixed_index_in_range(source.type, index))) {
        // An unknown or out-of-range index reads the element type's default
        // (IEEE 1800-2017 7.4.6).
        missing = default_container_value(
            aggregate_element_type(source.type));
        selected = &*missing;
    } else {
        const auto at = source.type.fixed
            ? operation.linear_index
                ? known_index(
                      process, instruction,
                      access.read(operation.index), true,
                      "multidimensional linear index")
                : fixed_offset(
                      process, instruction, source.type,
                      access.read(operation.index))
            : known_index(
                  process, instruction,
                  access.read(operation.index),
                  operation.signed_index, "container index");
        if (at >= source.nested_elements.size()) {
            container_error(
                process, instruction,
                "aggregate container index is out of range");
        }
        selected = &source.nested_elements[at];
    }
    for (const auto member : operation.members) {
        if (selected->type.element_kind != ContainerElementKind::Aggregate
            || member >= selected->nested_elements.size()) {
            container_error(
                process, instruction,
                "aggregate member read path is invalid");
        }
        selected = &selected->nested_elements[member];
    }
    if (!selected->type.fixed || selected->elements.size() != 1U) {
        container_error(
            process, instruction,
            "aggregate member read requires a packed or scalar leaf");
    }
    access.write(operation.destination, selected->elements.front());
}

void execute_container_operation(
    const ContainerAggregateWrite& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    apply_container_aggregate_member(target,
        access.read(operation.index), operation.signed_index,
        operation.linear_index, operation.members,
        access.read(operation.source), process, instruction);
}

void execute_container_operation(
    const CopyContainerAggregateElement& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    const auto& source = access.read_container(operation.source);
    if (target.type != source.type
        || target.type.element_kind != ContainerElementKind::Aggregate) {
        container_error(
            process, instruction,
            "aggregate element copy requires identical container profiles");
    }
    std::optional<ContainerValue> missing;
    const ContainerValue* source_element = nullptr;
    if (source.type.associative) {
        const auto key = associative_key(
            process, instruction, source.type,
            access.read(operation.source_index));
        const auto source_at = lower_key(source, key);
        if (source_at < source.keys.size()
            && key_equal(source.keys[source_at], key)) {
            source_element = &source.nested_elements[source_at];
        } else {
            missing = default_container_value(
                aggregate_element_type(source.type));
            source_element = &*missing;
        }
    } else {
        const auto source_at = source.type.fixed
            ? fixed_offset(
                  process, instruction, source.type,
                  access.read(operation.source_index))
            : known_index(
                  process, instruction,
                  access.read(operation.source_index),
                  operation.source_signed_index,
                  "source container index");
        if (source_at >= source.nested_elements.size()) {
            container_error(
                process, instruction,
                "aggregate element copy source index is out of range");
        }
        source_element = &source.nested_elements[source_at];
    }
    const auto snapshot = *source_element;
    std::size_t target_at { };
    if (target.type.associative) {
        const auto key = associative_key(
            process, instruction, target.type,
            access.read(operation.target_index));
        target_at = lower_key(target, key);
        if (target_at >= target.keys.size()
            || !key_equal(target.keys[target_at], key)) {
            if (target.keys.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process, instruction,
                    "associative aggregate exceeds its owning-storage budget");
            }
            target.keys.insert(
                iterator_at(target.keys, target_at), key);
            target.nested_elements.insert(
                iterator_at(target.nested_elements, target_at),
                default_container_value(
                    aggregate_element_type(target.type)));
        }
    } else {
        target_at = target.type.fixed
            ? fixed_offset(
                  process, instruction, target.type,
                  access.read(operation.target_index))
            : known_index(
                  process, instruction,
                  access.read(operation.target_index),
                  operation.target_signed_index,
                  "target container index");
        if (target_at >= target.nested_elements.size()) {
            container_error(
                process, instruction,
                "aggregate element copy target index is out of range");
        }
    }
    if (target.nested_elements[target_at].type != snapshot.type) {
        container_error(
            process, instruction,
            "aggregate element copy profile mismatch");
    }
    target.nested_elements[target_at] = snapshot;
}

void execute_container_operation(
    const DeleteContainer& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    // Aggregate and nested-container elements are value-owned in
    // nested_elements.
    const bool aggregate = target.type.element_kind == ContainerElementKind::Aggregate
        || target.type.element_kind == ContainerElementKind::Container;
    const bool string_element = target.type.element_kind == ContainerElementKind::String;
    if (operation.index) {
        if (target.type.queue) {
            const auto requested
                = access.read(*operation.index).known_signed_value();
            const auto size = aggregate
                ? target.nested_elements.size()
                : string_element ? target.string_elements.size()
                                 : target.elements.size();
            // An unknown, negative or out-of-range index leaves the queue
            // unchanged (IEEE 1800-2017 7.10.2.3).
            if (!requested || *requested < 0
                || static_cast<std::uint64_t>(*requested) >= size) {
                return;
            }
            const auto at = static_cast<std::size_t>(*requested);
            if (aggregate) {
                target.nested_elements.erase(
                    iterator_at(target.nested_elements, at));
            } else if (string_element) {
                target.string_elements.erase(
                    iterator_at(target.string_elements, at));
            } else {
                target.elements.erase(iterator_at(target.elements, at));
            }
        } else {
            require_associative(
                process, instruction, target, "delete(index)");
            if (operation.string_index) {
                const auto& key = associative_string_key(
                    process, instruction, target.type,
                    access.read_string(*operation.index));
                const auto at = lower_string_key(target, key);
                if (at < target.string_keys.size()
                    && target.string_keys[at] == key) {
                    target.string_keys.erase(
                        iterator_at(target.string_keys, at));
                    if (aggregate) {
                        target.nested_elements.erase(
                            iterator_at(target.nested_elements, at));
                    } else if (target.type.element_kind
                        == ContainerElementKind::String) {
                        target.string_elements.erase(
                            iterator_at(target.string_elements, at));
                    } else {
                        target.elements.erase(iterator_at(target.elements, at));
                    }
                }
                return;
            }
            const auto key = associative_key(
                process, instruction, target.type,
                access.read(*operation.index));
            const auto at = lower_key(target, key);
            if (at < target.keys.size()
                && key_equal(target.keys[at], key)) {
                target.keys.erase(iterator_at(target.keys, at));
                if (aggregate) {
                    target.nested_elements.erase(
                        iterator_at(target.nested_elements, at));
                } else if (string_element) {
                    target.string_elements.erase(
                        iterator_at(target.string_elements, at));
                } else {
                    target.elements.erase(iterator_at(target.elements, at));
                }
            }
        }
    } else {
        if (target.type.fixed) {
            container_error(
                process, instruction,
                "delete() cannot clear a static array");
        }
        if (aggregate) {
            target.nested_elements.clear();
        } else if (target.type.element_kind
            == ContainerElementKind::String) {
            target.string_elements.clear();
        } else {
            target.elements.clear();
        }
        target.keys.clear();
        target.string_keys.clear();
    }
}

void execute_container_operation(
    const ContainerExists& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    const auto& source = access.read_container(operation.source);
    require_associative(
        process, instruction, source, "exists(index)");
    if (operation.string_index) {
        const auto& key = associative_string_key(
            process, instruction, source.type,
            access.read_string(operation.index));
        const auto at = lower_string_key(source, key);
        const auto exists = at < source.string_keys.size()
            && source.string_keys[at] == key;
        access.write(operation.destination, PackedLogic4::from_aval_bval(32, exists ? 1U : 0U, 0));
        return;
    }
    const auto key = associative_key(
        process, instruction, source.type,
        access.read(operation.index));
    const auto at = lower_key(source, key);
    const auto exists = at < source.keys.size() && key_equal(source.keys[at], key);
    access.write(operation.destination, PackedLogic4::from_aval_bval(32, exists ? 1U : 0U, 0));
}

void execute_container_operation(
    const TraverseContainer& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    const auto& source = access.read_container(operation.source);
    require_associative(
        process, instruction, source,
        "first/last/next/prev");
    if (operation.string_index) {
        std::optional<std::size_t> selected;
        if (!source.string_keys.empty()) {
            if (operation.traversal == ContainerTraversal::first) {
                selected = 0;
            } else if (operation.traversal == ContainerTraversal::last) {
                selected = source.string_keys.size() - 1U;
            } else {
                const auto& key = associative_string_key(
                    process, instruction, source.type,
                    access.read_string(operation.index));
                const auto at = lower_string_key(source, key);
                if (operation.traversal == ContainerTraversal::next) {
                    const auto next = at < source.string_keys.size()
                            && source.string_keys[at] == key
                        ? at + 1U
                        : at;
                    if (next < source.string_keys.size())
                        selected = next;
                } else if (at != 0) {
                    selected = at - 1U;
                }
            }
        }
        if (selected) {
            access.string(operation.index) = source.string_keys[*selected];
        }
        access.write(operation.destination, PackedLogic4::from_aval_bval(
            32, selected.has_value() ? 1U : 0U, 0));
        return;
    }
    std::optional<std::size_t> selected;
    if (!source.keys.empty()) {
        if (operation.traversal == ContainerTraversal::first) {
            selected = 0;
        } else if (operation.traversal == ContainerTraversal::last) {
            selected = source.keys.size() - 1U;
        } else {
            const auto key = associative_key(
                process, instruction, source.type,
                access.read(operation.index));
            const auto at = lower_key(source, key);
            if (operation.traversal == ContainerTraversal::next) {
                const auto next = at < source.keys.size()
                        && key_equal(source.keys[at], key)
                    ? at + 1U
                    : at;
                if (next < source.keys.size()) {
                    selected = next;
                }
            } else if (at != 0) {
                selected = at - 1U;
            }
        }
    }
    if (selected) {
        access.write(operation.index, source.keys[*selected]);
    }
    access.write(operation.destination, PackedLogic4::from_aval_bval(
        32, selected.has_value() ? 1U : 0U, 0));
}

void execute_container_operation(
    const PushContainer& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    require_queue(process, instruction, target);
    const auto& source = access.read(operation.source);
    if (source.width() != target.type.element_width
        || source.is_logic9()
        || (target.type.two_state && has_unknown(source))) {
        container_error(
            process, instruction,
            "queue element write type mismatch");
    }
    if (operation.index) {
        const auto requested
            = access.read(*operation.index).known_signed_value();
        // An unknown, negative or out-of-range index inserts nothing
        // (IEEE 1800-2017 7.10.2.2).
        if (!requested || *requested < 0
            || static_cast<std::uint64_t>(*requested)
                > target.elements.size()) {
            return;
        }
        target.elements.insert(
            iterator_at(target.elements,
                static_cast<std::size_t>(*requested)),
            source);
    } else if (operation.front) {
        target.elements.insert(target.elements.begin(), source);
    } else {
        target.elements.push_back(source);
    }
    const auto storage_limit = maximum_container_elements(target.type);
    if (target.elements.size() > storage_limit
        && (!target.type.maximum_elements
            || *target.type.maximum_elements > storage_limit)) {
        target.elements.pop_back();
        container_error(
            process, instruction,
            "queue exceeds the per-container owning-storage budget");
    }
    if (target.type.maximum_elements
        && target.elements.size() > *target.type.maximum_elements) {
        target.elements.pop_back();
    }
}

void execute_container_operation(
    const PopContainer& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    require_queue(process, instruction, target);
    // Popping an empty queue returns the element type's default value
    // (IEEE 1800-2017 7.10.2.4, 7.10.2.5).
    if (target.elements.empty()) {
        access.write(operation.destination, PackedLogic4(
            target.type.element_width,
            target.type.two_state ? Logic4::zero : Logic4::x));
        return;
    }
    auto value = operation.front
        ? target.elements.front()
        : target.elements.back();
    if (operation.front) {
        target.elements.erase(target.elements.begin());
    } else {
        target.elements.pop_back();
    }
    access.write(operation.destination, std::move(value));
}

void execute_container_operation(
    const AppendContainer& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& target = access.container(operation.target);
    if (target.type.fixed || target.type.associative
        || target.type.aggregate_value) {
        container_error(process, instruction,
            "an unpacked array concatenation requires a queue or dynamic array");
    }
    if (operation.clear) {
        target.elements.clear();
        target.string_elements.clear();
        target.nested_elements.clear();
    }
    if (operation.container) {
        const auto source
            = access.read_container(*operation.container);
        if (source.type.element_kind != target.type.element_kind
            || source.type.associative
            || ((target.type.element_kind == ContainerElementKind::Packed
                    || target.type.element_kind
                        == ContainerElementKind::Scalar)
                && source.type.element_width != target.type.element_width)) {
            container_error(process, instruction,
                "an unpacked array concatenation operand has an incompatible element type");
        }
        target.elements.insert(target.elements.end(),
            source.elements.begin(), source.elements.end());
        target.string_elements.insert(target.string_elements.end(),
            source.string_elements.begin(), source.string_elements.end());
        target.nested_elements.insert(target.nested_elements.end(),
            source.nested_elements.begin(), source.nested_elements.end());
    }
    if (operation.value) {
        auto value = access.read(*operation.value);
        if (value.width() != target.type.element_width) {
            container_error(process, instruction,
                "an unpacked array concatenation element has the wrong width");
        }
        target.elements.push_back(std::move(value));
    }
    if (operation.text) {
        target.string_elements.push_back(
            access.read_string(*operation.text));
    }
    // A bounded queue keeps its first elements (7.10.5).
    if (target.type.maximum_elements) {
        const auto limit = static_cast<std::size_t>(
            *target.type.maximum_elements);
        if (target.elements.size() > limit) {
            target.elements.resize(limit);
        }
        if (target.string_elements.size() > limit) {
            target.string_elements.resize(limit);
        }
        if (target.nested_elements.size() > limit) {
            target.nested_elements.resize(limit);
        }
    }
}

void execute_container_operation(
    const ContainerBitstream& operation,
    ContainerRegisterAccess& access,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& bits = access.container(operation.bits);
    switch (operation.operation) {
    case ContainerBitstreamOperation::clear:
        bits.elements.clear();
        break;
    case ContainerBitstreamOperation::append_value: {
        const auto& value = access.read(operation.value);
        for (std::size_t bit = value.width(); bit-- > 0U;) {
            PackedLogic4 single(1U, Logic4::zero);
            single.set(0U, value.get(bit));
            bits.elements.push_back(std::move(single));
        }
        break;
    }
    case ContainerBitstreamOperation::append_container: {
        const auto source
            = access.read_container(operation.container);
        append_container_bits(
            process, instruction, bits.elements, source);
        break;
    }
    case ContainerBitstreamOperation::reverse: {
        // The slices are taken from the right end of the stream, the
        // last one possibly short (IEEE 1800-2017 11.4.14.2).
        const std::size_t slice = std::max<std::uint32_t>(operation.slice, 1U);
        std::vector<PackedLogic4> reordered;
        reordered.reserve(bits.elements.size());
        auto end = bits.elements.size();
        while (end > 0U) {
            const auto begin = end > slice ? end - slice : 0U;
            for (auto bit = begin; bit < end; ++bit) {
                reordered.push_back(bits.elements[bit]);
            }
            end = begin;
        }
        bits.elements = std::move(reordered);
        break;
    }
    case ContainerBitstreamOperation::to_value: {
        // The destination register is checked before the stream is examined.
        static_cast<void>(access.read(operation.value));
        const std::size_t width = operation.slice;
        if (bits.elements.size() > width) {
            container_error(process, instruction,
                "a bit stream of " + std::to_string(bits.elements.size())
                    + " bits is wider than its " + std::to_string(width)
                    + "-bit target");
        }
        PackedLogic4 result(width, Logic4::zero);
        for (std::size_t index { }; index < bits.elements.size(); ++index) {
            result.set(width - 1U - index, bits.elements[index].get(0U));
        }
        access.write(operation.value, std::move(result));
        break;
    }
    case ContainerBitstreamOperation::take_value: {
        static_cast<void>(access.read(operation.value));
        const std::size_t width = operation.slice;
        if (bits.elements.size() < width) {
            container_error(process, instruction,
                "a bit stream is smaller than its targets");
        }
        PackedLogic4 result(width, Logic4::zero);
        for (std::size_t index { }; index < width; ++index) {
            result.set(width - 1U - index, bits.elements[index].get(0U));
        }
        bits.elements.erase(bits.elements.begin(),
            bits.elements.begin() + static_cast<std::ptrdiff_t>(width));
        access.write(operation.value, std::move(result));
        break;
    }
    case ContainerBitstreamOperation::take_container: {
        auto& destination
            = access.container(operation.container);
        std::size_t position { };
        // `slice` holds the bits reserved for the later fixed-size targets.
        consume_container_bits(process, instruction, bits.elements,
            position, destination, true, operation.slice);
        if (position > bits.elements.size()) {
            container_error(process, instruction,
                "a bit stream is smaller than its targets");
        }
        bits.elements.erase(bits.elements.begin(),
            bits.elements.begin() + static_cast<std::ptrdiff_t>(position));
        break;
    }
    case ContainerBitstreamOperation::to_container: {
        auto& destination
            = access.container(operation.container);
        std::size_t position { };
        consume_container_bits(process, instruction, bits.elements,
            position, destination, true);
        if (position < bits.elements.size()) {
            container_error(process, instruction,
                "a bit stream is wider than its target");
        }
        break;
    }
    }
}

} // namespace fsim::runtime::simir
