// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_container_helpers.hpp"

#include <algorithm>
#include <charconv>

namespace fsim::runtime::simir {

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerStringWrite& operation)
{
    auto& target = get_container_register(process, operation.target);
    if (operation.associative_default) {
        if (!target.type.associative) {
            container_error(process.id, process.pc,
                "a container default requires an associative array");
        }
        target.associative_default_text
            = get_string_register(process, operation.source);
        ++process.pc;
        return;
    }
    if (!operation.members.empty()) {
        if (target.type.element_kind != ContainerElementKind::Aggregate
            || target.type.associative || operation.string_index) {
            container_error(
                process.id, process.pc,
                "aggregate string member write requires an indexed aggregate container");
        }
        const auto at = target.type.aggregate_value ? std::size_t { }
            : target.type.fixed
            ? operation.linear_index
                ? known_index(
                      process.id, process.pc,
                      get_register(process, operation.index), true,
                      "multidimensional linear index")
                : fixed_offset(
                      process.id, process.pc, target.type,
                      get_register(process, operation.index))
            : known_index(
                  process.id, process.pc,
                  get_register(process, operation.index),
                  operation.signed_index, "container index");
        if (!target.type.aggregate_value
            && at >= target.nested_elements.size()) {
            container_error(
                process.id, process.pc,
                "aggregate container index is out of range");
        }
        auto* selected = target.type.aggregate_value
            ? &target
            : &target.nested_elements[at];
        for (const auto member : operation.members) {
            if (selected->type.element_kind != ContainerElementKind::Aggregate
                || member >= selected->nested_elements.size()) {
                container_error(
                    process.id, process.pc,
                    "aggregate string member write path is invalid");
            }
            selected = &selected->nested_elements[member];
        }
        if (selected->type.element_kind != ContainerElementKind::String
            || !selected->type.fixed
            || selected->string_elements.size() != 1U) {
            container_error(
                process.id, process.pc,
                "aggregate string member write requires a scalar string leaf");
        }
        selected->string_elements.front()
            = get_string_register(process, operation.source);
        ++process.pc;
        return;
    }
    if (target.type.element_kind != ContainerElementKind::String) {
        container_error(
            process.id, process.pc,
            "string element write requires a string container");
    }
    const auto source = get_string_register(process, operation.source);
    if (target.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process.id, process.pc, target.type,
                get_string_register(process, operation.index));
            const auto at = lower_string_key(target, key);
            if (at < target.string_keys.size()
                && target.string_keys[at] == key) {
                target.string_elements[at] = source;
            } else {
                if (target.string_keys.size()
                    >= maximum_container_elements(target.type)) {
                    container_error(
                        process.id, process.pc,
                        "associative string array exceeds its owning-storage budget");
                }
                target.string_keys.insert(
                    iterator_at(target.string_keys, at), key);
                target.string_elements.insert(
                    iterator_at(target.string_elements, at), source);
            }
            validate_container_value(target);
            ++process.pc;
            return;
        }
        const auto key = associative_key(
            process.id, process.pc, target.type,
            get_register(process, operation.index));
        const auto at = lower_key(target, key);
        if (at < target.keys.size() && key_equal(target.keys[at], key)) {
            target.string_elements[at] = source;
        } else {
            if (target.keys.size() >= maximum_container_elements(target.type)) {
                container_error(
                    process.id, process.pc,
                    "associative string array exceeds its owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.string_elements.insert(
                iterator_at(target.string_elements, at), source);
        }
        ++process.pc;
        return;
    }
    // A queue or dynamic array ignores a write through an unknown,
    // negative or out-of-range index; a queue write one past its last
    // element appends (IEEE 1800-2017 7.4.6, 7.10.1).
    if (!target.type.fixed && !operation.linear_index) {
        const auto& raw = get_register(process, operation.index);
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
            ++process.pc;
            return;
        }
        if (static_cast<std::uint64_t>(*requested) == size) {
            target.string_elements.push_back(source);
            ++process.pc;
            return;
        }
    }
    const auto at = target.type.fixed
        ? operation.linear_index
            ? known_index(
                  process.id, process.pc,
                  get_register(process, operation.index), true,
                  "multidimensional linear index")
            : fixed_offset(
                  process.id, process.pc, target.type,
                  get_register(process, operation.index))
        : known_index(
              process.id, process.pc,
              get_register(process, operation.index),
              operation.signed_index, "container index");
    if (at >= target.string_elements.size()) {
        container_error(
            process.id, process.pc,
            "string container index is out of range");
    }
    target.string_elements[at] = source;
    ++process.pc;
}

namespace {

// Follow an aggregate member path from one aggregate value.
template <typename Value>
[[nodiscard]] Value* follow_container_members(
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

}  // namespace

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerElementRead& operation)
{
    const auto& source = read_container_register(process, operation.source);
    const ContainerValue* selected = nullptr;
    std::optional<ContainerValue> missing;
    if (aggregate_box(source.type)) {
        selected = &source;
    } else {
        const auto element_type = nested_element_container_type(source.type);
        if (!element_type) {
            container_error(
                process.id, process.pc,
                "nested element read requires a nested container");
        }
        if (source.type.associative && operation.string_index) {
            const auto& key = associative_string_key(
                process.id, process.pc, source.type,
                get_string_register(process, operation.index));
            const auto at = lower_string_key(source, key);
            if (at < source.string_keys.size()
                && source.string_keys[at] == key) {
                selected = &source.nested_elements[at];
            }
        } else {
            const auto& index = get_register(process, operation.index);
            if (source.type.associative) {
                const auto key = associative_key(
                    process.id, process.pc, source.type, index);
                const auto at = lower_key(source, key);
                if (at < source.keys.size()
                    && key_equal(source.keys[at], key)) {
                    selected = &source.nested_elements[at];
                }
            } else if (source.type.fixed && operation.linear_index) {
                const auto at = known_index(
                    process.id, process.pc, index, true,
                    "multidimensional linear index");
                if (at < source.nested_elements.size()) {
                    selected = &source.nested_elements[at];
                }
            } else if (source.type.fixed) {
                if (fixed_index_in_range(source.type, index)) {
                    const auto at = fixed_offset(
                        process.id, process.pc, source.type, index);
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
        process.id, process.pc, selected, operation.members);
    auto& destination = get_container_register(process, operation.destination);
    if (destination.type != selected->type) {
        container_error(
            process.id, process.pc,
            "nested element read profile mismatch");
    }
    destination = *selected;
    ++process.pc;
}

ContainerValue* Interpreter::Impl::select_container_element_for_write(
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

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerElementWrite& operation)
{
    auto& target = get_container_register(process, operation.target);
    const auto& source = read_container_register(process, operation.source);
    auto* selected = operation.string_index
        ? select_container_element_for_write(target, PackedLogic4 { },
              false, false, process.id, process.pc,
              &get_string_register(process, operation.index))
        : select_container_element_for_write(
              target, get_register(process, operation.index),
              operation.signed_index, operation.linear_index, process.id,
              process.pc);
    if (selected == nullptr) {
        ++process.pc;
        return;
    }
    selected = follow_container_members(
        process.id, process.pc, selected, operation.members);
    if (selected->type != source.type) {
        container_error(
            process.id, process.pc,
            "nested element write profile mismatch");
    }
    *selected = source;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerAggregateRead& operation)
{
    const auto& source = read_container_register(process, operation.source);
    if (source.type.element_kind != ContainerElementKind::Aggregate
        || operation.members.empty()) {
        container_error(
            process.id, process.pc,
            "aggregate member read requires an unpacked aggregate container");
    }
    std::optional<ContainerValue> missing;
    const ContainerValue* selected = nullptr;
    if (aggregate_box(source.type)) {
        selected = &source;
    } else if (source.type.associative) {
        const auto key = associative_key(
            process.id, process.pc, source.type,
            get_register(process, operation.index));
        const auto at = lower_key(source, key);
        if (at < source.keys.size()
            && key_equal(source.keys[at], key)) {
            selected = &source.nested_elements[at];
        } else {
            missing = default_container_value(
                aggregate_element_type(source.type));
            selected = &*missing;
        }
    } else if (const auto& index = get_register(process, operation.index);
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
                      process.id, process.pc,
                      get_register(process, operation.index), true,
                      "multidimensional linear index")
                : fixed_offset(
                      process.id, process.pc, source.type,
                      get_register(process, operation.index))
            : known_index(
                  process.id, process.pc,
                  get_register(process, operation.index),
                  operation.signed_index, "container index");
        if (at >= source.nested_elements.size()) {
            container_error(
                process.id, process.pc,
                "aggregate container index is out of range");
        }
        selected = &source.nested_elements[at];
    }
    for (const auto member : operation.members) {
        if (selected->type.element_kind != ContainerElementKind::Aggregate
            || member >= selected->nested_elements.size()) {
            container_error(
                process.id, process.pc,
                "aggregate member read path is invalid");
        }
        selected = &selected->nested_elements[member];
    }
    if (!selected->type.fixed || selected->elements.size() != 1U) {
        container_error(
            process.id, process.pc,
            "aggregate member read requires a packed or scalar leaf");
    }
    get_register(process, operation.destination) = selected->elements.front();
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerAggregateWrite& operation)
{
    auto& target = get_container_register(process, operation.target);
    apply_container_aggregate_member(target,
        get_register(process, operation.index), operation.signed_index,
        operation.linear_index, operation.members,
        get_register(process, operation.source), process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::write_container_object_member_value(
    const ContainerObjectId id,
    const PackedLogic4& index,
    const bool signed_index,
    const bool linear_index,
    const std::span<const std::uint32_t> members,
    const PackedLogic4& value,
    const ProcessId process,
    const InstructionIndex instruction,
    const SignalChangeOrigin origin)
{
    auto replacement = read_container_object_value(id);
    apply_container_aggregate_member(replacement, index, signed_index,
        linear_index, members, value, process, instruction);
    write_container_object_value(id, replacement, process, origin);
}

void Interpreter::Impl::write_container_object_path_value(
    const ContainerObjectId id,
    const std::span<const ContainerPathLevel> levels,
    const std::span<const PackedLogic4> level_indices,
    const PackedLogic4& index,
    const bool signed_index,
    const bool linear_index,
    const std::span<const std::uint32_t> members,
    const PackedLogic4& value,
    const PackedLogic4* base,
    const DynamicPartIndex* selection,
    const ProcessId process,
    const InstructionIndex instruction,
    const SignalChangeOrigin origin)
{
    if (levels.size() != level_indices.size()
        || (selection != nullptr && base == nullptr)) {
        container_error(
            process, instruction, "container path write is malformed");
    }
    auto replacement = read_container_object_value(id);
    ContainerValue* selected = &replacement;
    for (std::size_t level { }; level < levels.size(); ++level) {
        selected = select_container_element_for_write(*selected,
            level_indices[level], levels[level].signed_index,
            levels[level].linear_index, process, instruction);
        if (selected == nullptr) {
            return;
        }
        selected = follow_container_members(
            process, instruction, selected, levels[level].members);
    }
    if (!members.empty()) {
        apply_container_aggregate_member(*selected, index, signed_index,
            linear_index, members, value, process, instruction);
        write_container_object_value(id, replacement, process, origin);
        return;
    }
    auto& target = *selected;
    if (target.type.element_kind != ContainerElementKind::Packed
        && target.type.element_kind != ContainerElementKind::Scalar) {
        container_error(
            process, instruction, "container element write type mismatch");
    }
    std::size_t at { };
    if (target.type.associative) {
        if (target.type.string_indices) {
            container_error(
                process, instruction,
                "container path writes require an integral key");
        }
        const auto key = associative_key(
            process, instruction, target.type, index);
        at = lower_key(target, key);
        if (at >= target.keys.size() || !key_equal(target.keys[at], key)) {
            if (target.elements.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process, instruction,
                    "associative array exceeds the per-container "
                    "owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.elements.insert(iterator_at(target.elements, at),
                initial_packed_element(target.type));
        }
    } else if (target.type.fixed) {
        // An unknown or out-of-range index writes nothing (7.4.6).
        if (!linear_index && !fixed_index_in_range(target.type, index)) {
            return;
        }
        at = linear_index
            ? known_index(
                  process, instruction, index, true,
                  "multidimensional linear index")
            : fixed_offset(process, instruction, target.type, index);
        if (at >= target.elements.size()) {
            container_error(
                process, instruction, "container index is out of range");
        }
    } else {
        // A queue or dynamic array ignores a write through an unknown,
        // negative or out-of-range index; a queue write one past its last
        // element appends (IEEE 1800-2017 7.4.6, 7.10.1).
        std::optional<std::int64_t> requested;
        if (signed_index) {
            requested = index.known_signed_value();
        } else if (const auto known = index.known_unsigned_value();
                   known
                   && *known <= static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max())) {
            requested = static_cast<std::int64_t>(*known);
        }
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
            target.elements.push_back(initial_packed_element(target.type));
        }
        at = static_cast<std::size_t>(*requested);
    }
    auto element = value;
    if (selection != nullptr) {
        try {
            element = dynamic_part_insert_value(
                target.elements[at], value, *base, *selection);
        } catch (const std::invalid_argument&) {
            container_error(
                process, instruction,
                "invalid dynamic part-select container element write");
        }
    }
    if (element.width() != target.type.element_width
        || element.is_logic9()
        || (target.type.two_state && has_unknown(element))) {
        container_error(
            process, instruction, "container element write type mismatch");
    }
    target.elements[at] = std::move(element);
    write_container_object_value(id, replacement, process, origin);
}

void Interpreter::Impl::apply_container_aggregate_member(
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

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CopyContainerAggregateElement& operation)
{
    auto& target = get_container_register(process, operation.target);
    const auto& source = read_container_register(process, operation.source);
    if (target.type != source.type
        || target.type.element_kind != ContainerElementKind::Aggregate) {
        container_error(
            process.id, process.pc,
            "aggregate element copy requires identical container profiles");
    }
    std::optional<ContainerValue> missing;
    const ContainerValue* source_element = nullptr;
    if (source.type.associative) {
        const auto key = associative_key(
            process.id, process.pc, source.type,
            get_register(process, operation.source_index));
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
                  process.id, process.pc, source.type,
                  get_register(process, operation.source_index))
            : known_index(
                  process.id, process.pc,
                  get_register(process, operation.source_index),
                  operation.source_signed_index,
                  "source container index");
        if (source_at >= source.nested_elements.size()) {
            container_error(
                process.id, process.pc,
                "aggregate element copy source index is out of range");
        }
        source_element = &source.nested_elements[source_at];
    }
    const auto snapshot = *source_element;
    std::size_t target_at { };
    if (target.type.associative) {
        const auto key = associative_key(
            process.id, process.pc, target.type,
            get_register(process, operation.target_index));
        target_at = lower_key(target, key);
        if (target_at >= target.keys.size()
            || !key_equal(target.keys[target_at], key)) {
            if (target.keys.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process.id, process.pc,
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
                  process.id, process.pc, target.type,
                  get_register(process, operation.target_index))
            : known_index(
                  process.id, process.pc,
                  get_register(process, operation.target_index),
                  operation.target_signed_index,
                  "target container index");
        if (target_at >= target.nested_elements.size()) {
            container_error(
                process.id, process.pc,
                "aggregate element copy target index is out of range");
        }
    }
    if (target.nested_elements[target_at].type != snapshot.type) {
        container_error(
            process.id, process.pc,
            "aggregate element copy profile mismatch");
    }
    target.nested_elements[target_at] = snapshot;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const DeleteContainer& operation)
{
    auto& target = get_container_register(process, operation.target);
    // Aggregate and nested-container elements are value-owned in
    // nested_elements.
    const bool aggregate = target.type.element_kind == ContainerElementKind::Aggregate
        || target.type.element_kind == ContainerElementKind::Container;
    const bool string_element = target.type.element_kind == ContainerElementKind::String;
    if (operation.index) {
        if (target.type.queue) {
            const auto requested
                = get_register(process, *operation.index).known_signed_value();
            const auto size = aggregate
                ? target.nested_elements.size()
                : string_element ? target.string_elements.size()
                                 : target.elements.size();
            // An unknown, negative or out-of-range index leaves the queue
            // unchanged (IEEE 1800-2017 7.10.2.3).
            if (!requested || *requested < 0
                || static_cast<std::uint64_t>(*requested) >= size) {
                ++process.pc;
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
                process.id, process.pc, target, "delete(index)");
            if (operation.string_index) {
                const auto& key = associative_string_key(
                    process.id, process.pc, target.type,
                    get_string_register(process, *operation.index));
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
                ++process.pc;
                return;
            }
            const auto key = associative_key(
                process.id, process.pc, target.type,
                get_register(process, *operation.index));
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
                process.id, process.pc,
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
    ++process.pc;
}

void Interpreter::Impl::read_memory_file(const ProcessId process,
    const InstructionIndex pc, const std::string& path, ContainerValue& target,
    const bool hexadecimal, const std::optional<std::int32_t> start,
    const std::optional<std::int32_t> finish)
{
    const auto handle = open_file(process, path, "r");
    // A file that cannot be opened loads nothing; say so rather than
    // reporting an empty file's word count (IEEE 1800-2023 21.4).
    if (handle == 0) {
        const auto message
            = "cannot open read-memory file '" + path + "'";
        if (!report_hook) {
            container_error(process, pc, message);
        } else {
            report_hook(process, message, AssertionSeverity::error,
                SourceLocation { }, scheduler.now(), scheduler.delta());
        }
        return;
    }
    std::string text;
    try {
        while (!file_end_of_file(process, handle)) {
            std::uint32_t count { };
            auto line = read_file_line(
                process, handle, count);
            if (text.size() + line.size()
                > maximum_memory_file_bytes) {
                throw std::length_error {
                    "read-memory file exceeds the 1 MiB limit"
                };
            }
            text += line;
        }
        close_file(process, handle);
    } catch (...) {
        try {
            close_file(process, handle);
        } catch (...) {
        }
        throw;
    }
    try {
        const auto load = load_memory_text_with_status(
            target, text, hexadecimal, start, finish);
        if (load.word_count_mismatch && report_hook) {
            report_hook(
                process,
                "read-memory file word count differs from the selected range",
                AssertionSeverity::warning,
                SourceLocation { },
                scheduler.now(), scheduler.delta());
        }
    } catch (const std::exception& error) {
        // A malformed file or an address outside the range ends the load
        // with an error message; simulation continues (IEEE 1800-2017
        // 21.4).
        if (!report_hook) {
            container_error(
                process, pc, error.what());
        } else {
            report_hook(process, error.what(), AssertionSeverity::error,
                SourceLocation { }, scheduler.now(), scheduler.delta());
        }
    }
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const LoadMemory& operation)
{
    auto& target = get_container_register(process, operation.target);
    const auto optional_integer =
        [&](const std::optional<RegisterId> source,
            const std::string_view role)
        -> std::optional<std::int32_t> {
        if (!source) {
            return std::nullopt;
        }
        const auto& value = get_register(process, *source);
        const auto converted = value.known_signed_value();
        if (!converted
            || *converted < std::numeric_limits<std::int32_t>::min()
            || *converted > std::numeric_limits<std::int32_t>::max()) {
            container_error(
                process.id, process.pc,
                std::string { role }
                    + " must be a known 32-bit integral value");
        }
        return static_cast<std::int32_t>(*converted);
    };
    const auto start = optional_integer(
        operation.start, operation.write ? "write-memory start" : "read-memory start");
    const auto finish = optional_integer(
        operation.finish, operation.write ? "write-memory finish" : "read-memory finish");
    if (operation.write) {
        const auto text = write_memory_text(
            target, operation.hexadecimal, start, finish);
        const auto handle = open_file(
            process.id,
            get_string_register(process, operation.path), "w");
        try {
            write_file(process.id, handle, text, false);
            close_file(process.id, handle);
        } catch (...) {
            try {
                close_file(process.id, handle);
            } catch (...) {
            }
            throw;
        }
        ++process.pc;
        return;
    }
    read_memory_file(process.id, process.pc,
        get_string_register(process, operation.path), target,
        operation.hexadecimal, start, finish);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const VitalMemoryDeclare& operation)
{
    auto memory = make_vital_memory(
        operation.word_count, operation.word_width,
        operation.subword_width);
    const auto& path = get_string_register(process, operation.load_file);
    if (operation.embedded_load) {
        load_vital_memory_text(
            memory, operation.embedded_load_text, operation.binary);
    } else if (!path.empty()) {
        const auto handle = open_file(process.id, path, "r");
        std::string text;
        try {
            while (!file_end_of_file(process.id, handle)) {
                std::uint32_t count { };
                auto line = read_file_line(
                    process.id, handle, count);
                if (text.size() + line.size() > maximum_memory_file_bytes) {
                    throw std::length_error {
                        "VITAL memory load file exceeds the 1 MiB input budget"
                    };
                }
                text += line;
            }
            close_file(process.id, handle);
        } catch (...) {
            try {
                close_file(process.id, handle);
            } catch (...) {
            }
            throw;
        }
        load_vital_memory_text(memory, text, operation.binary);
    }
    if (process.frame->vital_memories.size()
        >= std::numeric_limits<std::uint32_t>::max()) {
        container_error(
            process.id, process.pc,
            "VITAL memory handle space is exhausted");
    }
    process.frame->vital_memories.push_back(std::move(memory));
    get_register(process, operation.destination) = PackedLogic4::from_aval_bval(
        32, process.frame->vital_memories.size(), 0U);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerExists& operation)
{
    const auto& source = read_container_register(process, operation.source);
    require_associative(
        process.id, process.pc, source, "exists(index)");
    if (operation.string_index) {
        const auto& key = associative_string_key(
            process.id, process.pc, source.type,
            get_string_register(process, operation.index));
        const auto at = lower_string_key(source, key);
        const auto exists = at < source.string_keys.size()
            && source.string_keys[at] == key;
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(32, exists ? 1U : 0U, 0);
        ++process.pc;
        return;
    }
    const auto key = associative_key(
        process.id, process.pc, source.type,
        get_register(process, operation.index));
    const auto at = lower_key(source, key);
    const auto exists = at < source.keys.size() && key_equal(source.keys[at], key);
    get_register(process, operation.destination) = PackedLogic4::from_aval_bval(32, exists ? 1U : 0U, 0);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const TraverseContainer& operation)
{
    const auto& source = read_container_register(process, operation.source);
    require_associative(
        process.id, process.pc, source,
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
                    process.id, process.pc, source.type,
                    get_string_register(process, operation.index));
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
            get_string_register(process, operation.index) = source.string_keys[*selected];
        }
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(
            32, selected.has_value() ? 1U : 0U, 0);
        ++process.pc;
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
                process.id, process.pc, source.type,
                get_register(process, operation.index));
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
        get_register(process, operation.index) = source.keys[*selected];
    }
    get_register(process, operation.destination) = PackedLogic4::from_aval_bval(
        32, selected.has_value() ? 1U : 0U, 0);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const PushContainer& operation)
{
    auto& target = get_container_register(process, operation.target);
    require_queue(process.id, process.pc, target);
    const auto& source = get_register(process, operation.source);
    if (source.width() != target.type.element_width
        || source.is_logic9()
        || (target.type.two_state && has_unknown(source))) {
        container_error(
            process.id, process.pc,
            "queue element write type mismatch");
    }
    if (operation.index) {
        const auto requested
            = get_register(process, *operation.index).known_signed_value();
        // An unknown, negative or out-of-range index inserts nothing
        // (IEEE 1800-2017 7.10.2.2).
        if (!requested || *requested < 0
            || static_cast<std::uint64_t>(*requested)
                > target.elements.size()) {
            ++process.pc;
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
            process.id, process.pc,
            "queue exceeds the per-container owning-storage budget");
    }
    if (target.type.maximum_elements
        && target.elements.size() > *target.type.maximum_elements) {
        target.elements.pop_back();
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const PopContainer& operation)
{
    auto& target = get_container_register(process, operation.target);
    require_queue(process.id, process.pc, target);
    // Popping an empty queue returns the element type's default value
    // (IEEE 1800-2017 7.10.2.4, 7.10.2.5).
    if (target.elements.empty()) {
        get_register(process, operation.destination) = PackedLogic4(
            target.type.element_width,
            target.type.two_state ? Logic4::zero : Logic4::x);
        ++process.pc;
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
    get_register(process, operation.destination) = std::move(value);
    ++process.pc;
}

} // namespace fsim::runtime::simir

namespace fsim::runtime::simir {
namespace {

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

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerBitstream& operation)
{
    auto& bits = get_container_register(process, operation.bits);
    switch (operation.operation) {
    case ContainerBitstreamOperation::clear:
        bits.elements.clear();
        break;
    case ContainerBitstreamOperation::append_value: {
        const auto& value = get_register(process, operation.value);
        for (std::size_t bit = value.width(); bit-- > 0U;) {
            PackedLogic4 single(1U, Logic4::zero);
            single.set(0U, value.get(bit));
            bits.elements.push_back(std::move(single));
        }
        break;
    }
    case ContainerBitstreamOperation::append_container: {
        const auto source
            = read_container_register(process, operation.container);
        append_container_bits(
            process.id, process.pc, bits.elements, source);
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
        auto& destination = get_register(process, operation.value);
        const std::size_t width = operation.slice;
        if (bits.elements.size() > width) {
            container_error(process.id, process.pc,
                "a bit stream of " + std::to_string(bits.elements.size())
                    + " bits is wider than its " + std::to_string(width)
                    + "-bit target");
        }
        PackedLogic4 result(width, Logic4::zero);
        for (std::size_t index { }; index < bits.elements.size(); ++index) {
            result.set(width - 1U - index, bits.elements[index].get(0U));
        }
        destination = std::move(result);
        break;
    }
    case ContainerBitstreamOperation::take_value: {
        auto& destination = get_register(process, operation.value);
        const std::size_t width = operation.slice;
        if (bits.elements.size() < width) {
            container_error(process.id, process.pc,
                "a bit stream is smaller than its targets");
        }
        PackedLogic4 result(width, Logic4::zero);
        for (std::size_t index { }; index < width; ++index) {
            result.set(width - 1U - index, bits.elements[index].get(0U));
        }
        bits.elements.erase(bits.elements.begin(),
            bits.elements.begin() + static_cast<std::ptrdiff_t>(width));
        destination = std::move(result);
        break;
    }
    case ContainerBitstreamOperation::take_container: {
        auto& destination
            = get_container_register(process, operation.container);
        std::size_t position { };
        // `slice` holds the bits reserved for the later fixed-size targets.
        consume_container_bits(process.id, process.pc, bits.elements,
            position, destination, true, operation.slice);
        if (position > bits.elements.size()) {
            container_error(process.id, process.pc,
                "a bit stream is smaller than its targets");
        }
        bits.elements.erase(bits.elements.begin(),
            bits.elements.begin() + static_cast<std::ptrdiff_t>(position));
        break;
    }
    case ContainerBitstreamOperation::to_container: {
        auto& destination
            = get_container_register(process, operation.container);
        std::size_t position { };
        consume_container_bits(process.id, process.pc, bits.elements,
            position, destination, true);
        if (position < bits.elements.size()) {
            container_error(process.id, process.pc,
                "a bit stream is wider than its target");
        }
        break;
    }
    }
    ++process.pc;
}

} // namespace fsim::runtime::simir
