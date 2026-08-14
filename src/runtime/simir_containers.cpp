// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <charconv>

namespace fsim::runtime::simir {

namespace {

    [[nodiscard]] bool aggregate_box(
        const ContainerType& type) noexcept
    {
        return type.element_kind == ContainerElementKind::Aggregate
            && type.aggregate_value;
    }

    [[nodiscard]] ContainerType aggregate_element_type(
        const ContainerType& type)
    {
        auto result = type;
        result.queue = false;
        result.associative = false;
        result.fixed = false;
        result.aggregate_value = true;
        result.maximum_elements.reset();
        result.dimensions.clear();
        result.index_left = 0;
        result.index_right = 0;
        return result;
    }

    [[nodiscard]] PackedLogic4 initial_packed_element(
        const ContainerType& type)
    {
        if (type.element_kind == ContainerElementKind::Scalar) {
            return PackedLogic4(type.element_width, Logic4::zero);
        }
        return type.two_state
            ? PackedLogic4(type.element_width, Logic4::zero)
            : PackedLogic4 { type.element_width, Logic4::x };
    }

    void append_default_element(ContainerValue& value)
    {
        switch (value.type.element_kind) {
        case ContainerElementKind::Packed:
        case ContainerElementKind::Scalar:
            value.elements.push_back(initial_packed_element(value.type));
            return;
        case ContainerElementKind::String:
            value.string_elements.emplace_back();
            return;
        case ContainerElementKind::Container:
            value.nested_elements.push_back(
                default_container_value(value.type.element_types.front()));
            return;
        case ContainerElementKind::Aggregate:
            value.nested_elements.push_back(
                default_container_value(aggregate_element_type(value.type)));
            return;
        }
    }

    void copy_element_prefix(
        ContainerValue& target,
        const ContainerValue& source,
        const std::size_t count)
    {
        switch (target.type.element_kind) {
        case ContainerElementKind::Packed:
        case ContainerElementKind::Scalar:
            std::ranges::copy_n(
                source.elements.begin(), static_cast<std::ptrdiff_t>(count),
                target.elements.begin());
            return;
        case ContainerElementKind::String:
            std::ranges::copy_n(
                source.string_elements.begin(),
                static_cast<std::ptrdiff_t>(count),
                target.string_elements.begin());
            return;
        case ContainerElementKind::Container:
        case ContainerElementKind::Aggregate:
            std::ranges::copy_n(
                source.nested_elements.begin(),
                static_cast<std::ptrdiff_t>(count),
                target.nested_elements.begin());
            return;
        }
    }

    template <typename Container>
    [[nodiscard]] auto iterator_at(
        Container& container,
        const std::size_t offset)
    {
        using Difference = typename Container::difference_type;
        return container.begin() + static_cast<Difference>(offset);
    }

    [[nodiscard]] std::size_t fixed_offset(
        const ContainerType& type,
        const std::int32_t index)
    {
        return static_cast<std::size_t>(
            type.index_left >= type.index_right
                ? static_cast<std::int64_t>(type.index_left) - index
                : static_cast<std::int64_t>(index) - type.index_left);
    }

    [[noreturn]] void container_error(
        const ProcessId process,
        const InstructionIndex instruction,
        const std::string_view message)
    {
        throw InterpreterError {
            process, instruction, std::string { message }
        };
    }

    [[nodiscard]] std::size_t known_index(
        const ProcessId process,
        const InstructionIndex instruction,
        const PackedLogic4& value,
        const bool signed_index,
        const std::string_view role)
    {
        if (signed_index) {
            const auto converted = value.known_signed_value();
            if (!converted) {
                container_error(
                    process, instruction,
                    std::string { role } + " must be a known integral value");
            }
            if (*converted < 0) {
                container_error(
                    process, instruction,
                    std::string { role } + " cannot be negative");
            }
            if (static_cast<std::uint64_t>(*converted)
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
                container_error(
                    process, instruction,
                    std::string { role } + " is too large");
            }
            return static_cast<std::size_t>(*converted);
        }

        const auto converted = value.known_unsigned_value();
        if (!converted) {
            container_error(
                process, instruction,
                std::string { role }
                    + " must be a known integral value");
        }
        if (*converted
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            container_error(
                process, instruction,
                std::string { role } + " is too large");
        }
        return static_cast<std::size_t>(*converted);
    }

    [[nodiscard]] std::int32_t known_fixed_index(
        const ProcessId process,
        const InstructionIndex instruction,
        const PackedLogic4& value)
    {
        const auto converted = value.known_signed_value();
        if (!converted
            || *converted < std::numeric_limits<std::int32_t>::min()
            || *converted > std::numeric_limits<std::int32_t>::max()) {
            container_error(
                process, instruction,
                "static-array index must be a known 32-bit integral value");
        }
        return static_cast<std::int32_t>(*converted);
    }

    [[nodiscard]] std::size_t fixed_offset(
        const ProcessId process,
        const InstructionIndex instruction,
        const ContainerType& type,
        const PackedLogic4& value)
    {
        const auto index = known_fixed_index(process, instruction, value);
        const auto low = std::min(type.index_left, type.index_right);
        const auto high = std::max(type.index_left, type.index_right);
        if (index < low || index > high) {
            container_error(
                process, instruction,
                "static-array index is out of range");
        }
        return static_cast<std::size_t>(
            type.index_left >= type.index_right
                ? static_cast<std::int64_t>(type.index_left) - index
                : static_cast<std::int64_t>(index) - type.index_left);
    }

    void require_same_type(
        const ProcessId process,
        const InstructionIndex instruction,
        const ContainerType& target,
        const ContainerType& source)
    {
        if (target != source) {
            container_error(
                process, instruction, "container value type mismatch");
        }
    }

    void require_queue(
        const ProcessId process,
        const InstructionIndex instruction,
        const ContainerValue& value)
    {
        if (!value.type.queue) {
            container_error(
                process, instruction,
                value.type.associative
                    ? "queue method used on an associative array"
                    : "queue method used on a dynamic array");
        }
    }

    void require_associative(
        const ProcessId process,
        const InstructionIndex instruction,
        const ContainerValue& value,
        const std::string_view operation)
    {
        if (!value.type.associative) {
            container_error(
                process, instruction,
                std::string { operation }
                    + " requires an associative array");
        }
    }

    [[nodiscard]] PackedLogic4 associative_key(
        const ProcessId process,
        const InstructionIndex instruction,
        const ContainerType& type,
        const PackedLogic4& value)
    {
        if (type.string_indices) {
            container_error(
                process, instruction,
                "string-indexed associative array requires a string index");
        }
        const auto unknown = value.is_logic9()
            || std::ranges::any_of(
                value.bval_words(), [](const auto word) { return word != 0; });
        if (value.width() != type.index_width || unknown) {
            container_error(
                process, instruction,
                unknown
                    ? "associative-array index must be a known integral value"
                    : "associative-array index type mismatch");
        }
        return value;
    }

    [[nodiscard]] const std::string& associative_string_key(
        const ProcessId process,
        const InstructionIndex instruction,
        const ContainerType& type,
        const std::string& value)
    {
        if (!type.associative || !type.string_indices) {
            container_error(
                process, instruction,
                "integral-indexed associative array requires an integral index");
        }
        if (value.size() > maximum_string_bytes
            || !runtime::systemverilog_string_is_valid(value)) {
            container_error(
                process, instruction,
                "associative-array string index must be bounded strict UTF-8");
        }
        return value;
    }

    [[nodiscard]] bool key_less(
        const ContainerType& type,
        const PackedLogic4& left,
        const PackedLogic4& right)
    {
        if (type.signed_indices) {
            const auto lhs_negative = left.get(type.index_width - 1U) == Logic4::one;
            const auto rhs_negative = right.get(type.index_width - 1U) == Logic4::one;
            if (lhs_negative != rhs_negative) {
                return lhs_negative;
            }
        }
        const auto lhs = left.aval_words();
        const auto rhs = right.aval_words();
        for (auto index = lhs.size(); index != 0; --index) {
            if (lhs[index - 1U] != rhs[index - 1U]) {
                return lhs[index - 1U] < rhs[index - 1U];
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t lower_key(
        const ContainerValue& value,
        const PackedLogic4& key)
    {
        return static_cast<std::size_t>(
            std::lower_bound(
                value.keys.begin(), value.keys.end(), key,
                [&](const PackedLogic4& candidate,
                    const PackedLogic4& sought) {
                    return key_less(value.type, candidate, sought);
                })
            - value.keys.begin());
    }

    [[nodiscard]] bool key_equal(
        const PackedLogic4& left,
        const PackedLogic4& right)
    {
        return left == right;
    }

    [[nodiscard]] std::size_t lower_string_key(
        const ContainerValue& value,
        const std::string_view key)
    {
        return static_cast<std::size_t>(
            std::lower_bound(
                value.string_keys.begin(), value.string_keys.end(), key)
            - value.string_keys.begin());
    }

} // namespace

const ContainerValue&
Interpreter::Impl::read_container_object_value(
    const ContainerObjectId id)
{
    auto& object = get_container_object(id);
    if (!object.slice_alias) {
        const auto& alias = container_signal_aliases.at(id);
        if (alias && alias->readable) {
            unpack_container_signal_value(
                object.initial_value,
                get_signal(alias->signal).initial_value);
        }
        return object.initial_value;
    }
    const auto& alias = *object.slice_alias;
    const auto& source = read_container_object_value(alias.object);
    const auto count = object.initial_value.elements.size();
    const auto descending = alias.selected_left >= alias.selected_right;
    for (std::size_t ordinal = 0;
        ordinal < count;
        ++ordinal) {
        const auto selected_index = static_cast<std::int32_t>(
            static_cast<std::int64_t>(alias.selected_left)
            + (descending
                    ? -static_cast<std::int64_t>(ordinal)
                    : static_cast<std::int64_t>(ordinal)));
        object.initial_value.elements[ordinal] = source.elements[fixed_offset(source.type, selected_index)];
    }
    return object.initial_value;
}

void Interpreter::Impl::write_container_object_value(
    const ContainerObjectId id,
    const ContainerValue& value)
{
    validate_container_value(value);
    auto& object = get_container_object(id);
    if (object.initial_value.type != value.type) {
        throw std::invalid_argument {
            "container object write type mismatch"
        };
    }
    if (!object.slice_alias) {
        const bool changed = object.initial_value != value;
        object.initial_value = value;
        const auto& alias = container_signal_aliases.at(id);
        if (alias && alias->writable) {
            const bool logic9 = get_signal(alias->signal).initial_value.is_logic9();
            commit(
                alias->signal,
                pack_container_signal_value(value, logic9));
        }
        if (changed) {
            const auto waiters = container_dynamic_fanout.at(id);
            for (const auto process : waiters) {
                queue_next_delta(process);
            }
        }
        if (changed && container_object_change_hook) {
            container_object_change_hook(id, scheduler.now());
        }
        return;
    }
    const auto alias = *object.slice_alias;
    auto replacement = read_container_object_value(alias.object);
    const auto descending = alias.selected_left >= alias.selected_right;
    for (std::size_t ordinal = 0;
        ordinal < value.elements.size();
        ++ordinal) {
        const auto selected_index = static_cast<std::int32_t>(
            static_cast<std::int64_t>(alias.selected_left)
            + (descending
                    ? -static_cast<std::int64_t>(ordinal)
                    : static_cast<std::int64_t>(ordinal)));
        replacement.elements[fixed_offset(replacement.type, selected_index)] = value.elements[ordinal];
    }
    write_container_object_value(
        alias.object, replacement);
    object.initial_value = value;
}

PackedLogic4 default_container_element(
    const ContainerType& type)
{
    return PackedLogic4(type.element_width, Logic4::zero);
}

void resize_container_value(
    ContainerValue& target,
    const std::size_t size,
    const ContainerValue* initializer)
{
    validate_container_value(target);
    if (target.type.associative || target.type.fixed
        || aggregate_box(target.type)) {
        throw std::invalid_argument {
            "only a dynamic array or queue may be resized"
        };
    }
    if (size > maximum_container_elements(target.type)) {
        throw std::length_error {
            "dynamic array exceeds its owning-storage budget"
        };
    }
    if (initializer) {
        validate_container_value(*initializer);
        if (initializer->type != target.type) {
            throw std::invalid_argument {
                "dynamic-array initializer type mismatch"
            };
        }
    }
    const auto preserved = initializer ? container_value_size(*initializer) : 0;
    ContainerValue replacement;
    replacement.type = target.type;
    switch (target.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        replacement.elements.assign(size, initial_packed_element(target.type));
        break;
    case ContainerElementKind::String:
        replacement.string_elements.resize(size);
        break;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        replacement.nested_elements.reserve(size);
        for (std::size_t index = 0; index < size; ++index) {
            append_default_element(replacement);
        }
        break;
    }
    if (initializer) {
        copy_element_prefix(
            replacement, *initializer, std::min(size, preserved));
    }
    validate_container_value(replacement);
    target = std::move(replacement);
}

void select_container_value(
    ContainerValue& destination,
    const PackedLogic4& condition,
    const ContainerValue& when_true,
    const ContainerValue& when_false)
{
    validate_container_value(destination);
    validate_container_value(when_true);
    validate_container_value(when_false);
    if (destination.type != when_true.type
        || destination.type != when_false.type) {
        throw std::invalid_argument {
            "container conditional profiles differ"
        };
    }
    if (condition.width() != 1) {
        throw std::invalid_argument {
            "container conditional condition must be scalar"
        };
    }
    const auto state = condition.get(0);
    if (state == Logic4::one || state == Logic4::zero) {
        destination = state == Logic4::one ? when_true : when_false;
        return;
    }
    if (container_value_size(when_true)
            != container_value_size(when_false)
        || when_true.keys != when_false.keys) {
        destination = default_container_value(destination.type);
        return;
    }
    if (destination.type.element_kind != ContainerElementKind::Packed) {
        destination = when_true == when_false
            ? when_true
            : default_container_value(destination.type);
        return;
    }
    destination.keys = when_true.keys;
    destination.elements.clear();
    destination.elements.reserve(when_true.elements.size());
    for (std::size_t index = 0;
        index < when_true.elements.size(); ++index) {
        auto merged = conditional_value(
            condition,
            when_true.elements[index],
            when_false.elements[index]);
        if (destination.type.two_state) {
            auto coerced = PackedLogic4(
                destination.type.element_width, Logic4::zero);
            for (std::size_t bit = 0; bit < merged.width(); ++bit) {
                if (merged.get(bit) == Logic4::one) {
                    coerced.set(bit, Logic4::one);
                }
            }
            merged = std::move(coerced);
        }
        destination.elements.push_back(std::move(merged));
    }
}

PackedLogic4 compare_container_values(
    const ContainerValue& lhs,
    const ContainerValue& rhs,
    const bool case_equal)
{
    validate_container_value(lhs);
    validate_container_value(rhs);
    if (lhs.type != rhs.type) {
        throw std::invalid_argument {
            "container equality profiles differ"
        };
    }
    if (container_value_size(lhs) != container_value_size(rhs)
        || lhs.keys != rhs.keys) {
        return PackedLogic4(1, Logic4::zero);
    }
    if (lhs.type.element_kind == ContainerElementKind::String) {
        return PackedLogic4(
            1, lhs.string_elements == rhs.string_elements ? Logic4::one : Logic4::zero);
    }
    if (lhs.type.element_kind == ContainerElementKind::Container
        || lhs.type.element_kind == ContainerElementKind::Aggregate) {
        bool unknown { };
        for (std::size_t index = 0;
            index < lhs.nested_elements.size(); ++index) {
            const auto compared = compare_container_values(
                lhs.nested_elements[index], rhs.nested_elements[index], case_equal);
            const auto state = compared.get(0);
            if (state == Logic4::zero) {
                return PackedLogic4(1, Logic4::zero);
            }
            unknown |= state != Logic4::one;
        }
        return PackedLogic4(1, unknown ? Logic4::x : Logic4::one);
    }
    if (lhs.type.element_kind == ContainerElementKind::Scalar) {
        for (std::size_t index = 0; index < lhs.elements.size(); ++index) {
            const auto& left = lhs.elements[index];
            const auto& right = rhs.elements[index];
            bool equal { };
            if (lhs.type.scalar_kind == SystemVerilogScalarKind::Chandle
                || lhs.type.scalar_kind == SystemVerilogScalarKind::Time) {
                equal = left.low_word().aval == right.low_word().aval;
            } else {
                const auto left_scalar = decode_systemverilog_scalar_payload(
                    left, lhs.type.scalar_kind);
                const auto right_scalar = decode_systemverilog_scalar_payload(
                    right, rhs.type.scalar_kind);
                if (!left_scalar || !right_scalar) {
                    throw std::invalid_argument {
                        "SimIR scalar container payload is invalid"
                    };
                }
                const auto compared = systemverilog_scalar_compare(
                    SystemVerilogScalarComparison::Equal,
                    left_scalar.value, right_scalar.value);
                equal = compared && compared.value;
            }
            if (!equal)
                return PackedLogic4(1, Logic4::zero);
        }
        return PackedLogic4(1, Logic4::one);
    }
    bool unknown { };
    for (std::size_t element = 0;
        element < lhs.elements.size(); ++element) {
        const auto& left = lhs.elements[element];
        const auto& right = rhs.elements[element];
        for (std::size_t bit = 0; bit < lhs.type.element_width; ++bit) {
            const auto left_bit = left.get(bit);
            const auto right_bit = right.get(bit);
            if (case_equal) {
                if (left_bit != right_bit) {
                    return PackedLogic4(1, Logic4::zero);
                }
                continue;
            }
            const auto left_known = left_bit == Logic4::zero || left_bit == Logic4::one;
            const auto right_known = right_bit == Logic4::zero || right_bit == Logic4::one;
            if (left_known && right_known && left_bit != right_bit) {
                return PackedLogic4(1, Logic4::zero);
            }
            unknown |= !left_known || !right_known;
        }
    }
    return PackedLogic4(
        1, unknown ? Logic4::x : Logic4::one);
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ResizeContainer& operation)
{
    auto& target = get_container_register(process, operation.target);
    if ((target.type.queue && !operation.allow_queue)
        || target.type.associative || target.type.fixed) {
        container_error(
            process.program.id, process.pc,
            target.type.queue
                ? "new[size] cannot resize a queue"
                : target.type.associative
                ? "new[size] cannot resize an associative array"
                : "new[size] cannot resize a static array");
    }
    const auto size = known_index(
        process.program.id, process.pc,
        get_register(process, operation.size),
        false, "dynamic-array size");
    const ContainerValue* initializer { };
    if (operation.initializer) {
        const auto& source = get_container_register(process, *operation.initializer);
        if (source.type != target.type) {
            container_error(
                process.program.id, process.pc,
                "dynamic-array initializer type mismatch");
        }
        initializer = &source;
    }
    try {
        resize_container_value(target, size, initializer);
    } catch (const std::exception& error) {
        container_error(
            process.program.id, process.pc, error.what());
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CopyContainerRegister& operation)
{
    auto& destination = get_container_register(process, operation.destination);
    const auto& source = get_container_register(process, operation.source);
    require_same_type(
        process.program.id, process.pc,
        destination.type, source.type);
    destination = source;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ConditionalContainerSelect& operation)
{
    auto& destination = get_container_register(process, operation.destination);
    const auto& when_true = get_container_register(process, operation.when_true);
    const auto& when_false = get_container_register(process, operation.when_false);
    const auto& condition = get_register(process, operation.condition);
    try {
        select_container_value(
            destination, condition, when_true, when_false);
    } catch (const std::exception& error) {
        container_error(
            process.program.id, process.pc, error.what());
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CompareContainers& operation)
{
    try {
        get_register(process, operation.destination) = compare_container_values(
            get_container_register(process, operation.lhs),
            get_container_register(process, operation.rhs),
            operation.case_equal);
    } catch (const std::exception& error) {
        container_error(
            process.program.id, process.pc, error.what());
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ReadContainerObject& operation)
{
    auto& destination = get_container_register(process, operation.destination);
    const auto& source = read_container_object_value(operation.object);
    require_same_type(
        process.program.id, process.pc,
        destination.type, source.type);
    destination = source;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const WriteContainerObject& operation)
{
    const auto& source = get_container_register(process, operation.source);
    try {
        write_container_object_value(
            operation.object, source);
    } catch (const std::invalid_argument& error) {
        container_error(
            process.program.id, process.pc, error.what());
    }
    if (operation.transaction_signal) {
        stage_update(
            process.program.id,
            *operation.transaction_signal,
            PackedLogic4 { 1, Logic4::zero });
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerSize& operation)
{
    const auto size = container_value_size(get_container_register(
        process, operation.source));
    get_register(process, operation.destination) = PackedLogic4::from_aval_bval(32, size, 0);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerReduction& operation)
{
    get_register(process, operation.destination) = reduce_container_value(
        get_container_register(process, operation.source),
        operation.operation, operation.transformation);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const OrderContainer& operation)
{
    auto& target = get_container_register(process, operation.target);
    order_container_value(
        target, operation.operation, operation.key,
        [&]() { return next_random(process); });
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const LocateContainer& operation)
{
    auto& destination = get_container_register(process, operation.destination);
    const auto& source = get_container_register(process, operation.source);
    locate_container_values(
        destination, source, operation.operation,
        operation.predicate, operation.transformation);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerRead& operation)
{
    const auto& source = get_container_register(process, operation.source);
    if (source.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process.program.id, process.pc, source.type,
                get_string_register(process, operation.index));
            const auto at = lower_string_key(source, key);
            get_register(process, operation.destination) = at < source.string_keys.size()
                    && source.string_keys[at] == key
                ? source.elements[at]
                : default_container_element(source.type);
            ++process.pc;
            return;
        }
        const auto key = associative_key(
            process.program.id, process.pc, source.type,
            get_register(process, operation.index));
        const auto at = lower_key(source, key);
        get_register(process, operation.destination) = at < source.keys.size() && key_equal(source.keys[at], key)
            ? source.elements[at]
            : default_container_element(source.type);
        ++process.pc;
        return;
    }
    if (source.type.fixed) {
        if (operation.linear_index) {
            const auto index = known_index(
                process.program.id, process.pc,
                get_register(process, operation.index),
                true, "multidimensional linear index");
            if (index >= source.elements.size()) {
                container_error(
                    process.program.id, process.pc,
                    "multidimensional linear index is out of range");
            }
            get_register(process, operation.destination) = source.elements[index];
            ++process.pc;
            return;
        }
        get_register(process, operation.destination) = source.elements[fixed_offset(
            process.program.id, process.pc, source.type,
            get_register(process, operation.index))];
        ++process.pc;
        return;
    }
    const auto index = known_index(
        process.program.id, process.pc,
        get_register(process, operation.index),
        operation.signed_index, "container index");
    if (index >= source.elements.size()) {
        container_error(
            process.program.id, process.pc,
            "container index is out of range");
    }
    get_register(process, operation.destination) = source.elements[index];
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerWrite& operation)
{
    auto& target = get_container_register(process, operation.target);
    const auto& source = get_register(process, operation.source);
    if (source.width() != target.type.element_width
        || source.is_logic9()
        || (target.type.two_state && has_unknown(source))) {
        container_error(
            process.program.id, process.pc,
            "container element write type mismatch");
    }
    if (target.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process.program.id, process.pc, target.type,
                get_string_register(process, operation.index));
            const auto at = lower_string_key(target, key);
            if (at < target.string_keys.size()
                && target.string_keys[at] == key) {
                target.elements[at] = source;
            } else {
                if (target.elements.size()
                    >= maximum_container_elements(target.type)) {
                    container_error(
                        process.program.id, process.pc,
                        "associative array exceeds the per-container "
                        "owning-storage budget");
                }
                target.string_keys.insert(
                    iterator_at(target.string_keys, at), key);
                target.elements.insert(
                    iterator_at(target.elements, at), source);
            }
            validate_container_value(target);
            ++process.pc;
            return;
        }
        const auto key = associative_key(
            process.program.id, process.pc, target.type,
            get_register(process, operation.index));
        const auto at = lower_key(target, key);
        if (at < target.keys.size()
            && key_equal(target.keys[at], key)) {
            target.elements[at] = source;
        } else {
            if (target.elements.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process.program.id, process.pc,
                    "associative array exceeds the per-container "
                    "owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.elements.insert(iterator_at(target.elements, at), source);
        }
        ++process.pc;
        return;
    }
    if (target.type.fixed) {
        if (operation.linear_index) {
            const auto index = known_index(
                process.program.id, process.pc,
                get_register(process, operation.index),
                true, "multidimensional linear index");
            if (index >= target.elements.size()) {
                container_error(
                    process.program.id, process.pc,
                    "multidimensional linear index is out of range");
            }
            target.elements[index] = source;
            ++process.pc;
            return;
        }
        target.elements[fixed_offset(
            process.program.id, process.pc, target.type,
            get_register(process, operation.index))] = source;
        ++process.pc;
        return;
    }
    const auto index = known_index(
        process.program.id, process.pc,
        get_register(process, operation.index),
        operation.signed_index, "container index");
    if (index >= target.elements.size()) {
        container_error(
            process.program.id, process.pc,
            "container index is out of range");
    }
    target.elements[index] = source;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerStringRead& operation)
{
    const auto& source = get_container_register(process, operation.source);
    if (source.type.element_kind != ContainerElementKind::String) {
        container_error(
            process.program.id, process.pc,
            "string element read requires a string container");
    }
    if (source.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process.program.id, process.pc, source.type,
                get_string_register(process, operation.index));
            const auto at = lower_string_key(source, key);
            get_string_register(process, operation.destination) = at < source.string_keys.size()
                    && source.string_keys[at] == key
                ? source.string_elements[at]
                : std::string { };
            ++process.pc;
            return;
        }
        const auto key = associative_key(
            process.program.id, process.pc, source.type,
            get_register(process, operation.index));
        const auto at = lower_key(source, key);
        get_string_register(process, operation.destination) = at < source.keys.size() && key_equal(source.keys[at], key)
            ? source.string_elements[at]
            : std::string { };
        ++process.pc;
        return;
    }
    const auto at = source.type.fixed
        ? operation.linear_index
            ? known_index(
                  process.program.id, process.pc,
                  get_register(process, operation.index), true,
                  "multidimensional linear index")
            : fixed_offset(
                  process.program.id, process.pc, source.type,
                  get_register(process, operation.index))
        : known_index(
              process.program.id, process.pc,
              get_register(process, operation.index),
              operation.signed_index, "container index");
    if (at >= source.string_elements.size()) {
        container_error(
            process.program.id, process.pc,
            "string container index is out of range");
    }
    get_string_register(process, operation.destination) = source.string_elements[at];
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerStringWrite& operation)
{
    auto& target = get_container_register(process, operation.target);
    if (target.type.element_kind != ContainerElementKind::String) {
        container_error(
            process.program.id, process.pc,
            "string element write requires a string container");
    }
    const auto source = get_string_register(process, operation.source);
    if (target.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process.program.id, process.pc, target.type,
                get_string_register(process, operation.index));
            const auto at = lower_string_key(target, key);
            if (at < target.string_keys.size()
                && target.string_keys[at] == key) {
                target.string_elements[at] = source;
            } else {
                if (target.string_keys.size()
                    >= maximum_container_elements(target.type)) {
                    container_error(
                        process.program.id, process.pc,
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
            process.program.id, process.pc, target.type,
            get_register(process, operation.index));
        const auto at = lower_key(target, key);
        if (at < target.keys.size() && key_equal(target.keys[at], key)) {
            target.string_elements[at] = source;
        } else {
            if (target.keys.size() >= maximum_container_elements(target.type)) {
                container_error(
                    process.program.id, process.pc,
                    "associative string array exceeds its owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.string_elements.insert(
                iterator_at(target.string_elements, at), source);
        }
        ++process.pc;
        return;
    }
    const auto at = target.type.fixed
        ? operation.linear_index
            ? known_index(
                  process.program.id, process.pc,
                  get_register(process, operation.index), true,
                  "multidimensional linear index")
            : fixed_offset(
                  process.program.id, process.pc, target.type,
                  get_register(process, operation.index))
        : known_index(
              process.program.id, process.pc,
              get_register(process, operation.index),
              operation.signed_index, "container index");
    if (at >= target.string_elements.size()) {
        container_error(
            process.program.id, process.pc,
            "string container index is out of range");
    }
    target.string_elements[at] = source;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerElementRead& operation)
{
    const auto& source = get_container_register(process, operation.source);
    if (source.type.element_kind != ContainerElementKind::Container
        || source.type.element_types.size() != 1) {
        container_error(
            process.program.id, process.pc,
            "nested element read requires a nested container");
    }
    const ContainerValue* selected = nullptr;
    std::optional<ContainerValue> missing;
    if (source.type.associative) {
        const auto key = associative_key(
            process.program.id, process.pc, source.type,
            get_register(process, operation.index));
        const auto at = lower_key(source, key);
        if (at < source.keys.size()
            && key_equal(source.keys[at], key)) {
            selected = &source.nested_elements[at];
        } else {
            missing = default_container_value(source.type.element_types.front());
            selected = &*missing;
        }
    } else {
        const auto at = source.type.fixed
            ? fixed_offset(
                  process.program.id, process.pc, source.type,
                  get_register(process, operation.index))
            : known_index(
                  process.program.id, process.pc,
                  get_register(process, operation.index),
                  operation.signed_index, "container index");
        if (at >= source.nested_elements.size()) {
            container_error(
                process.program.id, process.pc,
                "nested container index is out of range");
        }
        selected = &source.nested_elements[at];
    }
    auto& destination = get_container_register(process, operation.destination);
    if (destination.type != selected->type) {
        container_error(
            process.program.id, process.pc,
            "nested element read profile mismatch");
    }
    destination = *selected;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerElementWrite& operation)
{
    auto& target = get_container_register(process, operation.target);
    const auto& source = get_container_register(process, operation.source);
    if (target.type.element_kind != ContainerElementKind::Container
        || target.type.element_types.size() != 1
        || target.type.element_types.front() != source.type) {
        container_error(
            process.program.id, process.pc,
            "nested element write profile mismatch");
    }
    std::size_t at { };
    if (target.type.associative) {
        const auto key = associative_key(
            process.program.id, process.pc, target.type,
            get_register(process, operation.index));
        at = lower_key(target, key);
        if (at >= target.keys.size()
            || !key_equal(target.keys[at], key)) {
            if (target.keys.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process.program.id, process.pc,
                    "associative nested container exceeds its owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.nested_elements.insert(
                iterator_at(target.nested_elements, at),
                default_container_value(target.type.element_types.front()));
        }
    } else {
        at = target.type.fixed
            ? fixed_offset(
                  process.program.id, process.pc, target.type,
                  get_register(process, operation.index))
            : known_index(
                  process.program.id, process.pc,
                  get_register(process, operation.index),
                  operation.signed_index, "container index");
        if (at >= target.nested_elements.size()) {
            container_error(
                process.program.id, process.pc,
                "nested container index is out of range");
        }
    }
    target.nested_elements[at] = source;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerAggregateRead& operation)
{
    const auto& source = get_container_register(process, operation.source);
    if (source.type.element_kind != ContainerElementKind::Aggregate
        || operation.members.empty()) {
        container_error(
            process.program.id, process.pc,
            "aggregate member read requires an unpacked aggregate container");
    }
    std::optional<ContainerValue> missing;
    const ContainerValue* selected = nullptr;
    if (source.type.associative) {
        const auto key = associative_key(
            process.program.id, process.pc, source.type,
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
    } else {
        const auto at = source.type.fixed
            ? operation.linear_index
                ? known_index(
                      process.program.id, process.pc,
                      get_register(process, operation.index), true,
                      "multidimensional linear index")
                : fixed_offset(
                      process.program.id, process.pc, source.type,
                      get_register(process, operation.index))
            : known_index(
                  process.program.id, process.pc,
                  get_register(process, operation.index),
                  operation.signed_index, "container index");
        if (at >= source.nested_elements.size()) {
            container_error(
                process.program.id, process.pc,
                "aggregate container index is out of range");
        }
        selected = &source.nested_elements[at];
    }
    for (const auto member : operation.members) {
        if (selected->type.element_kind != ContainerElementKind::Aggregate
            || member >= selected->nested_elements.size()) {
            container_error(
                process.program.id, process.pc,
                "aggregate member read path is invalid");
        }
        selected = &selected->nested_elements[member];
    }
    if (!selected->type.fixed || selected->elements.size() != 1U) {
        container_error(
            process.program.id, process.pc,
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
    if (target.type.element_kind != ContainerElementKind::Aggregate
        || operation.members.empty()) {
        container_error(
            process.program.id, process.pc,
            "aggregate member write requires an unpacked aggregate container");
    }
    std::size_t at { };
    if (target.type.associative) {
        const auto key = associative_key(
            process.program.id, process.pc, target.type,
            get_register(process, operation.index));
        at = lower_key(target, key);
        if (at >= target.keys.size()
            || !key_equal(target.keys[at], key)) {
            if (target.keys.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process.program.id, process.pc,
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
            ? operation.linear_index
                ? known_index(
                      process.program.id, process.pc,
                      get_register(process, operation.index), true,
                      "multidimensional linear index")
                : fixed_offset(
                      process.program.id, process.pc, target.type,
                      get_register(process, operation.index))
            : known_index(
                  process.program.id, process.pc,
                  get_register(process, operation.index),
                  operation.signed_index, "container index");
        if (at >= target.nested_elements.size()) {
            container_error(
                process.program.id, process.pc,
                "aggregate container index is out of range");
        }
    }
    auto* selected = &target.nested_elements[at];
    ContainerValue* direct_union = nullptr;
    for (std::size_t path_index = 0;
        path_index < operation.members.size(); ++path_index) {
        const auto member = operation.members[path_index];
        if (selected->type.element_kind != ContainerElementKind::Aggregate
            || member >= selected->nested_elements.size()) {
            container_error(
                process.program.id, process.pc,
                "aggregate member write path is invalid");
        }
        if (selected->type.union_aggregate
            && path_index + 1U == operation.members.size()) {
            direct_union = selected;
        }
        selected = &selected->nested_elements[member];
    }
    const auto& value = get_register(process, operation.source);
    if (!selected->type.fixed || selected->elements.size() != 1U
        || value.width() != selected->type.element_width
        || value.is_logic9()
        || (selected->type.two_state && has_unknown(value))) {
        container_error(
            process.program.id, process.pc,
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
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CopyContainerAggregateElement& operation)
{
    auto& target = get_container_register(process, operation.target);
    const auto& source = get_container_register(process, operation.source);
    if (target.type != source.type
        || target.type.element_kind != ContainerElementKind::Aggregate) {
        container_error(
            process.program.id, process.pc,
            "aggregate element copy requires identical container profiles");
    }
    std::optional<ContainerValue> missing;
    const ContainerValue* source_element = nullptr;
    if (source.type.associative) {
        const auto key = associative_key(
            process.program.id, process.pc, source.type,
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
                  process.program.id, process.pc, source.type,
                  get_register(process, operation.source_index))
            : known_index(
                  process.program.id, process.pc,
                  get_register(process, operation.source_index),
                  operation.source_signed_index,
                  "source container index");
        if (source_at >= source.nested_elements.size()) {
            container_error(
                process.program.id, process.pc,
                "aggregate element copy source index is out of range");
        }
        source_element = &source.nested_elements[source_at];
    }
    const auto snapshot = *source_element;
    std::size_t target_at { };
    if (target.type.associative) {
        const auto key = associative_key(
            process.program.id, process.pc, target.type,
            get_register(process, operation.target_index));
        target_at = lower_key(target, key);
        if (target_at >= target.keys.size()
            || !key_equal(target.keys[target_at], key)) {
            if (target.keys.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process.program.id, process.pc,
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
                  process.program.id, process.pc, target.type,
                  get_register(process, operation.target_index))
            : known_index(
                  process.program.id, process.pc,
                  get_register(process, operation.target_index),
                  operation.target_signed_index,
                  "target container index");
        if (target_at >= target.nested_elements.size()) {
            container_error(
                process.program.id, process.pc,
                "aggregate element copy target index is out of range");
        }
    }
    if (target.nested_elements[target_at].type != snapshot.type) {
        container_error(
            process.program.id, process.pc,
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
    const bool aggregate = target.type.element_kind == ContainerElementKind::Aggregate;
    const bool string_element = target.type.element_kind == ContainerElementKind::String;
    if (operation.index) {
        if (target.type.queue) {
            const auto at = known_index(
                process.program.id, process.pc,
                get_register(process, *operation.index), true,
                "queue delete index");
            const auto size = aggregate
                ? target.nested_elements.size()
                : string_element ? target.string_elements.size()
                                 : target.elements.size();
            if (at >= size) {
                container_error(
                    process.program.id, process.pc,
                    "queue delete index is out of range");
            }
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
                process.program.id, process.pc, target, "delete(index)");
            if (operation.string_index) {
                const auto& key = associative_string_key(
                    process.program.id, process.pc, target.type,
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
                process.program.id, process.pc, target.type,
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
                process.program.id, process.pc,
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
                process.program.id, process.pc,
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
            process.program.id,
            get_string_register(process, operation.path), "w");
        try {
            write_file(process.program.id, handle, text, false);
            close_file(process.program.id, handle);
        } catch (...) {
            try {
                close_file(process.program.id, handle);
            } catch (...) {
            }
            throw;
        }
        ++process.pc;
        return;
    }
    const auto handle = open_file(
        process.program.id,
        get_string_register(process, operation.path), "r");
    std::string text;
    try {
        while (!file_end_of_file(process.program.id, handle)) {
            std::uint32_t count { };
            auto line = read_file_line(
                process.program.id, handle, count);
            if (text.size() + line.size()
                > maximum_memory_file_bytes) {
                throw std::length_error {
                    "read-memory file exceeds the 1 MiB limit"
                };
            }
            text += line;
        }
        close_file(process.program.id, handle);
    } catch (...) {
        try {
            close_file(process.program.id, handle);
        } catch (...) {
        }
        throw;
    }
    try {
        load_memory_text(
            target, text, operation.hexadecimal,
            start, finish);
    } catch (const std::exception& error) {
        container_error(
            process.program.id, process.pc, error.what());
    }
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
        const auto handle = open_file(process.program.id, path, "r");
        std::string text;
        try {
            while (!file_end_of_file(process.program.id, handle)) {
                std::uint32_t count { };
                auto line = read_file_line(
                    process.program.id, handle, count);
                if (text.size() + line.size() > maximum_memory_file_bytes) {
                    throw std::length_error {
                        "VITAL memory load file exceeds the 1 MiB input budget"
                    };
                }
                text += line;
            }
            close_file(process.program.id, handle);
        } catch (...) {
            try {
                close_file(process.program.id, handle);
            } catch (...) {
            }
            throw;
        }
        load_vital_memory_text(memory, text, operation.binary);
    }
    if (process.frame->vital_memories.size()
        >= std::numeric_limits<std::uint32_t>::max()) {
        container_error(
            process.program.id, process.pc,
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
    const auto& source = get_container_register(process, operation.source);
    require_associative(
        process.program.id, process.pc, source, "exists(index)");
    if (operation.string_index) {
        const auto& key = associative_string_key(
            process.program.id, process.pc, source.type,
            get_string_register(process, operation.index));
        const auto at = lower_string_key(source, key);
        const auto exists = at < source.string_keys.size()
            && source.string_keys[at] == key;
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(32, exists ? 1U : 0U, 0);
        ++process.pc;
        return;
    }
    const auto key = associative_key(
        process.program.id, process.pc, source.type,
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
    const auto& source = get_container_register(process, operation.source);
    require_associative(
        process.program.id, process.pc, source,
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
                    process.program.id, process.pc, source.type,
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
                process.program.id, process.pc, source.type,
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
    require_queue(process.program.id, process.pc, target);
    const auto& source = get_register(process, operation.source);
    if (source.width() != target.type.element_width
        || source.is_logic9()
        || (target.type.two_state && has_unknown(source))) {
        container_error(
            process.program.id, process.pc,
            "queue element write type mismatch");
    }
    if (operation.index) {
        const auto at = known_index(
            process.program.id, process.pc,
            get_register(process, *operation.index), true,
            "queue insert index");
        if (at > target.elements.size()) {
            container_error(
                process.program.id, process.pc,
                "queue insert index is out of range");
        }
        target.elements.insert(iterator_at(target.elements, at), source);
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
            process.program.id, process.pc,
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
    require_queue(process.program.id, process.pc, target);
    if (target.elements.empty()) {
        container_error(
            process.program.id, process.pc,
            "cannot pop an empty queue");
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
