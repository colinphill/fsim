// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"


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

    [[maybe_unused]] void require_queue(
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

    [[maybe_unused]] void require_associative(
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
        if (value.size() > maximum_string_bytes) {
            container_error(
                process, instruction,
                "associative-array string index exceeds the byte limit");
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
            const auto revision = signal_value_revisions.at(alias->signal);
            if (container_materialized_revisions.at(id) != revision) {
                unpack_container_signal_value(
                    object.initial_value,
                    get_signal(alias->signal).initial_value);
                container_materialized_revisions[id] = revision;
            }
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

bool Interpreter::Impl::read_container_object_element(
    const ContainerObjectId id,
    const std::size_t ordinal,
    PackedLogic4& result)
{
    const auto& object = get_container_object(id);
    const auto& value = object.initial_value;
    if ((value.type.element_kind != ContainerElementKind::Packed
            && value.type.element_kind != ContainerElementKind::Scalar)
        || ordinal >= value.elements.size()) {
        return false;
    }
    if (object.slice_alias) {
        const auto& alias = *object.slice_alias;
        const auto descending = alias.selected_left >= alias.selected_right;
        const auto selected_index = static_cast<std::int32_t>(
            static_cast<std::int64_t>(alias.selected_left)
            + (descending
                    ? -static_cast<std::int64_t>(ordinal)
                    : static_cast<std::int64_t>(ordinal)));
        const auto& source = get_container_object(alias.object).initial_value;
        return read_container_object_element(
            alias.object,
            fixed_offset(source.type, selected_index),
            result);
    }
    const auto& alias = container_signal_aliases.at(id);
    if (!alias || !alias->readable) {
        result = value.elements[ordinal];
        return true;
    }
    const auto& packed = get_signal(alias->signal).initial_value;
    const auto width = value.type.element_width;
    if (width == 0 || ordinal >= packed.width() / width
        || packed.width() % width != 0) {
        return false;
    }
    result = extract_value(
        packed,
        packed.width() - (ordinal + 1U) * width,
        width);
    return true;
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
            container_materialized_revisions[id]
                = signal_value_revisions.at(alias->signal);
        }
        if (changed && (!alias || !alias->writable)) {
            const auto waiters = container_dynamic_fanout.at(id);
            for (const auto process : waiters) {
                queue_next_delta(process);
            }
        }
        if (changed && (!alias || !alias->writable)
            && container_object_change_hook) {
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

void Interpreter::Impl::write_container_object_element_value(
    const ContainerObjectId id,
    const PackedLogic4& index,
    const bool signed_index,
    const bool linear_index,
    const PackedLogic4& value,
    const ProcessId process,
    const InstructionIndex instruction)
{
    auto& object = get_container_object(id);
    auto& target = object.initial_value;
    if (object.slice_alias) {
        // Slice aliases need a coherent aggregate replacement. Ordinary
        // signal-backed element writes use the direct packed slice below and
        // must not materialize the whole container.
        (void)read_container_object_value(id);
        auto replacement = target;
        const auto selected = target.type.fixed
            ? linear_index
                ? known_index(
                      process, instruction, index, true,
                      "multidimensional linear index")
                : fixed_offset(
                      process, instruction, target.type, index)
            : known_index(
                  process, instruction, index, signed_index,
                  "container index");
        if (selected >= replacement.elements.size()) {
            container_error(
                process, instruction,
                "container index is out of range");
        }
        replacement.elements[selected] = value;
        write_container_object_value(id, replacement);
        return;
    }
    if ((target.type.element_kind != ContainerElementKind::Packed
            && target.type.element_kind != ContainerElementKind::Scalar)
        || value.width() != target.type.element_width
        || value.is_logic9()
        || (target.type.two_state && has_unknown(value))) {
        container_error(
            process, instruction,
            "container element write type mismatch");
    }
    const auto selected = target.type.fixed
        ? linear_index
            ? known_index(
                  process, instruction, index, true,
                  "multidimensional linear index")
            : fixed_offset(
                  process, instruction, target.type, index)
        : known_index(
              process, instruction, index, signed_index,
              "container index");
    if (selected >= target.elements.size()) {
        container_error(
            process, instruction,
            "container index is out of range");
    }
    const auto& alias = container_signal_aliases.at(id);
    const auto packed_offset = alias && alias->readable
        ? get_signal(alias->signal).initial_value.width()
            - (selected + 1U) * target.type.element_width
        : 0U;
    const auto materialized_revision
        = container_materialized_revisions.at(id);
    const auto signal_revision = alias
        ? signal_value_revisions.at(alias->signal)
        : 0U;
    const bool changed = alias && alias->readable
        ? extract_value(
              get_signal(alias->signal).initial_value,
              packed_offset,
              target.type.element_width) != value
        : target.elements[selected] != value;
    target.elements[selected] = value;
    if (alias && alias->writable) {
        const auto& signal = get_signal(alias->signal).initial_value;
        const auto offset = signal.width()
            - (selected + 1U) * target.type.element_width;
        commit_slice(alias->signal, value, offset);
        if (materialized_revision == signal_revision) {
            container_materialized_revisions[id]
                = signal_value_revisions.at(alias->signal);
        }
    }
    if (!changed) {
        return;
    }
    if (!alias || !alias->writable) {
        const auto waiters = container_dynamic_fanout.at(id);
        for (const auto waiter : waiters) {
            queue_next_delta(waiter);
        }
        if (container_object_change_hook) {
            container_object_change_hook(id, scheduler.now());
        }
    }
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
        const auto& source = read_container_register(
            process, *operation.initializer);
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
    const auto& source = read_container_register(process, operation.source);
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
    const auto& when_true = read_container_register(process, operation.when_true);
    const auto& when_false = read_container_register(process, operation.when_false);
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
            read_container_register(process, operation.lhs),
            read_container_register(process, operation.rhs),
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
    const auto& source = read_container_register(process, operation.source);
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
    const auto size = container_value_size(read_container_register(
        process, operation.source));
    get_register(process, operation.destination) = PackedLogic4::from_aval_bval(32, size, 0);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerReduction& operation)
{
    get_register(process, operation.destination) = reduce_container_value(
        read_container_register(process, operation.source),
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
    const auto& source = read_container_register(process, operation.source);
    locate_container_values(
        destination, source, operation.operation,
        operation.predicate, operation.transformation);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerRead& operation)
{
    const auto& source = read_container_register(process, operation.source);
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
        const auto& index_value = get_register(
            process, operation.index);
        if (operation.linear_index) {
            const auto index = index_value.known_signed_value();
            if (!index || *index < 0
                || static_cast<std::uint64_t>(*index)
                    >= source.elements.size()) {
                get_register(process, operation.destination) =
                    PackedLogic4(
                        source.type.element_width,
                        source.type.two_state
                            ? Logic4::zero : Logic4::x);
                ++process.pc;
                return;
            }
            get_register(process, operation.destination) =
                source.elements[static_cast<std::size_t>(*index)];
            ++process.pc;
            return;
        }
        const auto index = index_value.known_signed_value();
        const auto low = std::min(
            source.type.index_left, source.type.index_right);
        const auto high = std::max(
            source.type.index_left, source.type.index_right);
        if (!index || *index < low || *index > high) {
            get_register(process, operation.destination) =
                PackedLogic4(
                    source.type.element_width,
                    source.type.two_state
                        ? Logic4::zero : Logic4::x);
            ++process.pc;
            return;
        }
        get_register(process, operation.destination) = source.elements[
            fixed_offset(source.type, static_cast<std::int32_t>(*index))];
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
    const WriteContainerObjectElement& operation)
{
    const auto index = get_register(process, operation.index);
    const auto value = get_register(process, operation.source);
    if (operation.nonblocking) {
        scheduler.schedule(
            SchedulerPhase::update,
            process.program.id,
            [this, object = operation.object, index,
                signed_index = operation.signed_index,
                linear_index = operation.linear_index, value,
                driver = process.program.id,
                instruction = process.pc](Scheduler&) {
                write_container_object_element_value(
                    object, index, signed_index, linear_index, value,
                    driver, instruction);
            });
    } else {
        write_container_object_element_value(
            operation.object, index, operation.signed_index,
            operation.linear_index, value,
            process.program.id, process.pc);
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
    const ContainerStringRead& operation)
{
    const auto& source = read_container_register(process, operation.source);
    if (!operation.members.empty()) {
        if (source.type.element_kind != ContainerElementKind::Aggregate
            || source.type.associative || operation.string_index) {
            container_error(
                process.program.id, process.pc,
                "aggregate string member read requires an indexed aggregate container");
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
        if (at >= source.nested_elements.size()) {
            container_error(
                process.program.id, process.pc,
                "aggregate container index is out of range");
        }
        const auto* selected = &source.nested_elements[at];
        for (const auto member : operation.members) {
            if (selected->type.element_kind != ContainerElementKind::Aggregate
                || member >= selected->nested_elements.size()) {
                container_error(
                    process.program.id, process.pc,
                    "aggregate string member read path is invalid");
            }
            selected = &selected->nested_elements[member];
        }
        if (selected->type.element_kind != ContainerElementKind::String
            || !selected->type.fixed
            || selected->string_elements.size() != 1U) {
            container_error(
                process.program.id, process.pc,
                "aggregate string member read requires a scalar string leaf");
        }
        get_string_register(process, operation.destination)
            = selected->string_elements.front();
        ++process.pc;
        return;
    }
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

} // namespace fsim::runtime
