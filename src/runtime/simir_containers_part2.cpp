// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_container_helpers.hpp"
#include "simir_container_ops.hpp"
#include "fsim/runtime/simir_interpreter.hpp"

#include <stdexcept>


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
    const WriteContainerObjectString& operation)
{
    ProcessContainerAccess access { *this, process };
    StringElementCapture capture;
    const auto capture_index = [&](const RegisterId index, const bool text) {
        if (text) {
            capture.texts.push_back(access.read_string(index));
            capture.indices.emplace_back();
        } else {
            capture.indices.push_back(access.read(index));
            capture.texts.emplace_back();
        }
    };
    for (const auto& level : operation.levels) {
        capture_index(level.index, level.string_index);
    }
    capture_index(operation.index, operation.string_index);
    switch (operation.source_kind) {
    case ContainerElementSourceKind::text:
        capture.value = access.read_string(operation.source);
        break;
    case ContainerElementSourceKind::packed:
        capture.packed = access.read(operation.packed_source);
        break;
    case ContainerElementSourceKind::container:
        capture.container = access.read_container(operation.container_source);
        break;
    }
    write_object_string_element(operation,
        process.program().scheduling_domain(), process.id, process.pc,
        capture);
    ++process.pc;
}

namespace {

// The registers of one string element write against copies of the container
// object's value and of the nested containers it descends through: container
// register k holds level k's container (the last the leaf container), packed
// register k the index of level k, and string register k + 1 its key; string
// register 0 holds the new element value.
class CapturedStringElementAccess final : public ContainerRegisterAccess {
public:
    CapturedStringElementAccess(std::vector<ContainerValue>& containers,
        const std::vector<PackedLogic4>& indices,
        const std::vector<std::string>& texts, const std::string& value)
        : containers_ { containers }, indices_ { indices }, texts_ { texts },
          value_ { value }
    {
    }

    [[nodiscard]] const PackedLogic4& read(RegisterId id) override
    {
        return indices_.at(id);
    }
    void write(RegisterId, PackedLogic4) override
    {
        throw std::logic_error { "a captured string element write has no packed result" };
    }
    [[nodiscard]] const std::string& read_string(StringRegisterId id) override
    {
        return id == 0U ? value_ : texts_.at(id - 1U);
    }
    [[nodiscard]] std::string& string(StringRegisterId) override
    {
        throw std::logic_error { "a captured string element write has no string result" };
    }
    [[nodiscard]] ContainerValue& container(ContainerRegisterId id) override
    {
        return containers_.at(id);
    }
    [[nodiscard]] const ContainerValue& read_container(
        ContainerRegisterId id) override
    {
        return containers_.at(id);
    }
    [[nodiscard]] std::uint32_t next_random() override
    {
        return 0U;
    }

private:
    std::vector<ContainerValue>& containers_;
    const std::vector<PackedLogic4>& indices_;
    const std::vector<std::string>& texts_;
    const std::string& value_;
};

} // namespace

void Interpreter::Impl::write_object_string_element(
    const WriteContainerObjectString& operation,
    const ProcessSchedulingDomain process_domain, const ProcessId driver,
    const InstructionIndex instruction, const StringElementCapture& capture,
    const NonblockingSink& nonblocking_sink)
{
    const auto update_domain
        = process_domain == ProcessSchedulingDomain::systemverilog
        ? operation.nonblocking
            ? SignalUpdateDomain::systemverilog_nba
            : SignalUpdateDomain::systemverilog_active
        : SignalUpdateDomain::generic;
    auto origin = capture_signal_change_origin(driver, update_domain);
    if (update_domain == SignalUpdateDomain::generic
        && operation.nonblocking) {
        origin.phase = SchedulerPhase::update;
    }
    if (!operation.nonblocking) {
        write_container_object_string_element_value(
            operation, driver, instruction, capture, origin);
        return;
    }
    // The indices and the value are captured now; the element is written when
    // the update is published (IEEE 1800-2023 10.4.2).
    const auto publish = [this, operation, capture, driver, instruction,
            origin](Scheduler&) {
            write_container_object_string_element_value(
                operation, driver, instruction, capture, origin);
        };
    if (process_domain == ProcessSchedulingDomain::systemverilog
        && nonblocking_sink) {
        nonblocking_sink(publish);
    } else if (process_domain == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_systemverilog(origin.phase, driver, publish);
    } else {
        scheduler.schedule(SchedulerPhase::update, driver, publish);
    }
}

void Interpreter::Impl::write_container_object_string_element_value(
    const WriteContainerObjectString& operation, const ProcessId driver,
    const InstructionIndex instruction, const StringElementCapture& capture,
    const SignalChangeOrigin origin)
{
    const auto depth = operation.levels.size();
    std::vector<ContainerValue> containers;
    containers.reserve(depth + 2U);
    containers.push_back(read_container_object_value(operation.object));
    // The captured packed value follows the indices as one more packed
    // register, the captured container value the levels' containers as one
    // more container register.
    auto indices = capture.indices;
    const auto packed_register = static_cast<RegisterId>(indices.size());
    indices.push_back(capture.packed);
    CapturedStringElementAccess access {
        containers, indices, capture.texts, capture.value };
    // Register k of the captured indices is level k's; its key is string
    // register k + 1.
    const auto text_register = [](const std::size_t level) {
        return static_cast<RegisterId>(level + 1U);
    };
    for (std::size_t level = 0U; level < depth; ++level) {
        const auto& step = operation.levels[level];
        // The register that receives the element holds its type (as the
        // lowered register would): the nested element's, then the members'.
        const auto& from = containers[level].type;
        const auto element = aggregate_box(from)
            ? std::optional<ContainerType> { from }
            : nested_element_container_type(from);
        if (!element) {
            container_error(driver, instruction,
                "nested element write requires a nested container");
        }
        auto element_type = *element;
        for (const auto member : step.members) {
            if (member >= element_type.element_types.size()) {
                container_error(driver, instruction,
                    "aggregate member write path is invalid");
            }
            auto next = element_type.element_types[member];
            element_type = std::move(next);
        }
        containers.emplace_back();
        containers.back().type = std::move(element_type);
        ContainerElementRead read;
        read.destination = static_cast<ContainerRegisterId>(level + 1U);
        read.source = static_cast<ContainerRegisterId>(level);
        read.index = step.string_index
            ? text_register(level) : static_cast<RegisterId>(level);
        read.signed_index = step.signed_index;
        read.string_index = step.string_index;
        read.linear_index = step.linear_index;
        read.members = step.members;
        execute_container_operation(read, access, driver, instruction);
    }
    // The element write of the register form on the leaf container.
    const auto leaf_index = operation.string_index
        ? text_register(depth) : static_cast<RegisterId>(depth);
    switch (operation.source_kind) {
    case ContainerElementSourceKind::text: {
        const ContainerStringWrite write {
            static_cast<ContainerRegisterId>(depth), leaf_index,
            0U, operation.signed_index, operation.linear_index,
            operation.string_index, { }, false };
        execute_container_operation(write, access, driver, instruction);
        break;
    }
    case ContainerElementSourceKind::packed: {
        const ContainerWrite write {
            static_cast<ContainerRegisterId>(depth), leaf_index,
            packed_register, operation.signed_index, operation.linear_index,
            operation.string_index, false };
        execute_container_operation(write, access, driver, instruction);
        break;
    }
    case ContainerElementSourceKind::container: {
        const auto source = static_cast<ContainerRegisterId>(containers.size());
        containers.push_back(capture.container);
        ContainerElementWrite write;
        write.target = static_cast<ContainerRegisterId>(depth);
        write.index = leaf_index;
        write.source = source;
        write.signed_index = operation.signed_index;
        write.linear_index = operation.linear_index;
        write.string_index = operation.string_index;
        write.members = operation.members;
        execute_container_operation(write, access, driver, instruction);
        break;
    }
    }
    for (std::size_t level = depth; level-- > 0U;) {
        const auto& step = operation.levels[level];
        ContainerElementWrite back;
        back.target = static_cast<ContainerRegisterId>(level);
        back.index = step.string_index
            ? text_register(level) : static_cast<RegisterId>(level);
        back.source = static_cast<ContainerRegisterId>(level + 1U);
        back.signed_index = step.signed_index;
        back.string_index = step.string_index;
        back.linear_index = step.linear_index;
        back.members = step.members;
        execute_container_operation(back, access, driver, instruction);
    }
    write_container_object_value(
        operation.object, containers.front(), driver, origin);
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
    // Its reports carry the time of the loading process (current_time, a
    // closed static kernel's own time step too).
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
                SourceLocation { }, current_time(), current_delta());
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
                current_time(), current_delta());
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
                SourceLocation { }, current_time(), current_delta());
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
