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
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerElementRead& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerElementWrite& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerAggregateRead& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerAggregateWrite& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
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

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CopyContainerAggregateElement& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const DeleteContainer& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
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
        write_memory_file(process.id,
            get_string_register(process, operation.path), target,
            operation.hexadecimal, start, finish);
        ++process.pc;
        return;
    }
    read_memory_file(process.id, process.pc,
        get_string_register(process, operation.path), target,
        operation.hexadecimal, start, finish);
    ++process.pc;
}

void Interpreter::Impl::write_memory_file(const ProcessId process,
    const std::string& path, const ContainerValue& source,
    const bool hexadecimal, const std::optional<std::int32_t> start,
    const std::optional<std::int32_t> finish)
{
    const auto text = write_memory_text(source, hexadecimal, start, finish);
    const auto handle = open_file(process, path, "w");
    try {
        write_file(process, handle, text, false);
        close_file(process, handle);
    } catch (...) {
        try {
            close_file(process, handle);
        } catch (...) {
        }
        throw;
    }
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
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const TraverseContainer& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const PushContainer& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const PopContainer& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerBitstream& operation)
{
    ProcessContainerAccess access { *this, process };
    execute_container_operation(operation, access, process.id, process.pc);
    ++process.pc;
}

} // namespace fsim::runtime::simir
