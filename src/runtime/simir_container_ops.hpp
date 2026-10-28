// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime::simir {

/// The register file the register-only container operations run against.
/// The reference interpreter and the static kernel each implement it over
/// their own storage, so both execute the same operation code.
class ContainerRegisterAccess {
public:
    virtual ~ContainerRegisterAccess() = default;

    /// A packed register's value. An invalid register is reported as the
    /// reference does ("invalid register ID").
    [[nodiscard]] virtual const PackedLogic4& read(RegisterId id) = 0;
    /// Stores a packed register. The reference handlers store the value as
    /// it is (no value-kind conversion); an invalid register is reported
    /// after the value was computed.
    virtual void write(RegisterId id, PackedLogic4 value) = 0;

    /// A string register's value.
    [[nodiscard]] virtual const std::string& read_string(
        StringRegisterId id) = 0;
    /// A string register for assignment.
    [[nodiscard]] virtual std::string& string(StringRegisterId id) = 0;

    /// A container register for modification (copy-on-write: a register
    /// whose storage is shared is given its own copy first).
    [[nodiscard]] virtual ContainerValue& container(
        ContainerRegisterId id) = 0;
    /// A container register for reading; never copies.
    [[nodiscard]] virtual const ContainerValue& read_container(
        ContainerRegisterId id) = 0;

    /// The next draw of the executing process's random stream (the
    /// OrderContainer shuffle).
    [[nodiscard]] virtual std::uint32_t next_random() = 0;
};

// Register-only SystemVerilog container operations. Each runs the
// operation's semantics against `access`; `process` and `instruction`
// identify the executing process and instruction in the errors thrown.
// The caller advances the program counter after a normal return.
void execute_container_operation(const ResizeContainer&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const CopyContainerRegister&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ConditionalContainerSelect&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const CompareContainers&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerSize&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerReduction&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const OrderContainer&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const LocateContainer&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerRead&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerWrite&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerStringRead&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerStringWrite&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const FormatContainerPattern&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerElementRead&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerElementWrite&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerAggregateRead&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerAggregateWrite&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const CopyContainerAggregateElement&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const DeleteContainer&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerExists&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const TraverseContainer&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const PushContainer&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const PopContainer&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const AppendContainer&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);
void execute_container_operation(const ContainerBitstream&,
    ContainerRegisterAccess&, ProcessId, InstructionIndex);

// Follow an aggregate member path from one aggregate value.
[[nodiscard]] ContainerValue* follow_container_members(
    ProcessId process,
    InstructionIndex instruction,
    ContainerValue* selected,
    const std::vector<std::uint32_t>& members);
[[nodiscard]] const ContainerValue* follow_container_members(
    ProcessId process,
    InstructionIndex instruction,
    const ContainerValue* selected,
    const std::vector<std::uint32_t>& members);

// The nested element of `target` a write selects (adding an associative
// or appended queue element); null when the write is ignored.
[[nodiscard]] ContainerValue* select_container_element_for_write(
    ContainerValue& target,
    const PackedLogic4& index,
    bool signed_index,
    bool linear_index,
    ProcessId process,
    InstructionIndex instruction,
    const std::string* string_key = nullptr);

// Update one member of an unpacked aggregate element of a container
// value (index, then member path).
void apply_container_aggregate_member(
    ContainerValue& target,
    const PackedLogic4& index,
    bool signed_index,
    bool linear_index,
    std::span<const std::uint32_t> members,
    const PackedLogic4& value,
    ProcessId process,
    InstructionIndex instruction);

} // namespace fsim::runtime::simir
