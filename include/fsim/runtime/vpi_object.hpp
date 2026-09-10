// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_abi.h"
#include "fsim/runtime/vpi_types.hpp"
#include "fsim/runtime/vpi_value.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogVpiObjectKind : std::uint32_t {
    // Values 0-21 are the frozen v3 identities published before the complete
    // 2023 object inventory was added. New kinds are append-only.
    Root = 0,
    Module = 1,
    Interface = 2,
    Program = 3,
    Package = 4,
    GenerateScope = 5,
    Port = 6,
    Net = 7,
    Variable = 8,
    Parameter = 9,
    Memory = 10,
    Array = 11,
    Class = 12,
    ClassProperty = 13,
    NamedEvent = 14,
    /// Executable occurrence published beneath its owning hierarchy scope.
    Process = 15,
    /// Concurrent assertion occurrence with stable hierarchy identity.
    Assertion = 16,
    /// Read-only stored contribution, distinct from a signal's effective value.
    Driver = 17,
    /// Literal or parameter-folded expression value.
    Constant = 18,
    /// Concatenation expression with stable occurrence identity.
    Concatenation = 19,
    /// Unary, binary, or conditional operator occurrence.
    Operation = 20,
    /// Minimum/typical/maximum expression occurrence.
    MinTypMax = 21,
    ModuleArray,
    InterfaceArray,
    ProgramArray,
    Primitive,
    Udp,
    Gate,
    Switch,
    Modport,
    ClockingBlock,
    Task,
    Function,
    Method,
    SystemTaskCall,
    SystemFunctionCall,
    TaskCall,
    FunctionCall,
    Argument,
    GenerateScopeArray,
    GenVar,
    ContinuousAssignment,
    Assignment,
    Initial,
    Always,
    Begin,
    Fork,
    If,
    Case,
    CaseItem,
    For,
    While,
    Repeat,
    Forever,
    Wait,
    EventControl,
    DelayControl,
    Return,
    Disable,
    Force,
    Release,
    PropertyDeclaration,
    SequenceDeclaration,
    LetDeclaration,
    Covergroup,
    CoverPoint,
    CoverageCross,
    CoverageBin,
    Constraint,
    TypeSpecification,
    EnumerationConstant,
    StructureMember,
    PackedArrayType,
    UnpackedArrayType,
    QueueType,
    AssociativeArrayType,
    DynamicArrayType,
    StringType,
    ClassType,
    InterfaceType,
    ModportPort,
    ClockingIo,
    ParameterAssignment,
    PortBit,
    NetBit,
    VariableBit,
    MemoryWord,
    ArrayWord,
    Range,
    PartSelect,
    IndexedPartSelect,
    BitSelect,
    Expression,
    Call,
    UserSystemTaskFunction,
    Attribute,
    AttributeSpecification,
};

enum class SystemVerilogVpiObjectError {
    None,
    InvalidSimulation,
    InvalidHandle,
    CrossSimulation,
    StaleHandle,
    ReleasedHandle,
    InvalidKind,
    InvalidParent,
    InvalidName,
    DuplicateName,
    InvalidSource,
    InvalidType,
    NotFound,
    HasChildren,
    InvalidProperty,
    ResourceLimit,
};

enum class SystemVerilogVpiIteratorError {
    None,
    InvalidSimulation,
    InvalidObject,
    InvalidKind,
    InvalidRelationship,
    InvalidHandle,
    CrossSimulation,
    StaleHandle,
    ReleasedHandle,
    End,
    ResourceLimit,
};

enum class SystemVerilogVpiRelationshipKind : std::uint32_t {
    Children = 0,
    Parent,
    InternalScopes,
    Declarations,
    Ports,
    Nets,
    Variables,
    Parameters,
    Processes,
    Assertions,
    Drivers,
    Expressions,
    Arguments,
    Types,
    Coverage,
};

enum class SystemVerilogVpiPropertyKind : std::uint32_t {
    ObjectKind = 0,
    Parent,
    LiveChildren,
    Ordinal,
    Name,
    FullName,
    SourceFile,
    SourceLine,
    SourceColumn,
    Language,
    SemanticUnitId,
    SourceId,
    SemanticUnit,
    SourcePath,
    Standard,
    CompatibilityProfile,
    ValueCategory,
    NetKind,
    Direction,
    Lifetime,
    Width,
    IsSigned,
    IsConstant,
    HasTypeDescriptor,
};

enum class SystemVerilogVpiPropertyValueKind : std::uint32_t {
    ObjectKind = 0,
    Handle,
    UnsignedInteger,
    Boolean,
    String,
};

struct SystemVerilogVpiPropertyResult {
    SystemVerilogVpiPropertyValueKind kind {
        SystemVerilogVpiPropertyValueKind::UnsignedInteger
    };
    SystemVerilogVpiObjectKind object_kind {
        SystemVerilogVpiObjectKind::Root
    };
    fsim_vpi_handle_v1 handle { };
    std::uint64_t unsigned_integer { };
    bool boolean { };
    std::string string;
    SystemVerilogVpiObjectError error { SystemVerilogVpiObjectError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiObjectError::None;
    }
};

struct SystemVerilogVpiObjectCapabilities {
    std::uint32_t systemverilog_revision { 2023U };
    std::uint32_t object_kind_count { };
    std::uint32_t relationship_kind_count { };
    std::uint32_t property_kind_count { };
    std::uint32_t maximum_iterator_objects { };
    bool stable_numeric_identities { };
    bool source_locations { };
    bool type_provenance { };
    bool snapshot_iterators { };
};

using SystemVerilogVpiValueObserver = std::function<void(
    fsim_vpi_handle_v1,
    const SystemVerilogVpiStoredValue&)>;

/// Complete stored/effective state after a host-originated VPI mutation.
/// Kernel-originated `update_bound_value` publications intentionally do not
/// produce this event, which makes the observer suitable for a live runtime
/// bridge without recursive feedback. A non-success result rejects the host
/// mutation so a required kernel binding cannot silently diverge.
struct SystemVerilogVpiValueStateUpdate {
    fsim_vpi_handle_v1 object { };
    SystemVerilogVpiStoredValue value;
    std::optional<SystemVerilogVpiStoredValue> forced_value;
};

using SystemVerilogVpiValueStateObserver = std::function<
    SystemVerilogVpiValueError(const SystemVerilogVpiValueStateUpdate&)>;

struct SystemVerilogVpiSourceLocation {
    std::string file;
    std::uint32_t line { };
    std::uint32_t column { };
};

struct SystemVerilogVpiObjectDescriptor {
    SystemVerilogVpiObjectKind kind { SystemVerilogVpiObjectKind::Root };
    fsim_vpi_handle_v1 parent { };
    std::string name;
    std::optional<SystemVerilogVpiSourceLocation> source;
    std::optional<SystemVerilogVpiTypeInfo> type;
};

struct SystemVerilogVpiObjectInfo {
    fsim_vpi_handle_v1 handle { };
    fsim_vpi_handle_v1 parent { };
    SystemVerilogVpiObjectKind kind { SystemVerilogVpiObjectKind::Root };
    std::uint32_t live_children { };
    std::uint64_t ordinal { };
    std::string name;
    std::string full_name;
    std::optional<SystemVerilogVpiSourceLocation> source;
    std::optional<SystemVerilogVpiTypeInfo> type;
};

struct SystemVerilogVpiObjectResult {
    fsim_vpi_handle_v1 value { };
    SystemVerilogVpiObjectError error { SystemVerilogVpiObjectError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiObjectError::None && value != 0U;
    }
};

struct SystemVerilogVpiObjectLookupResult {
    std::optional<SystemVerilogVpiObjectInfo> value;
    SystemVerilogVpiObjectError error { SystemVerilogVpiObjectError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiObjectError::None && value.has_value();
    }
};

struct SystemVerilogVpiTypeLookupResult {
    std::optional<SystemVerilogVpiTypeInfo> value;
    SystemVerilogVpiObjectError error { SystemVerilogVpiObjectError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiObjectError::None && value.has_value();
    }
};

struct SystemVerilogVpiIteratorResult {
    fsim_vpi_handle_v1 value { };
    SystemVerilogVpiIteratorError error { SystemVerilogVpiIteratorError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiIteratorError::None && value != 0U;
    }
};

struct SystemVerilogVpiIteratorScanResult {
    fsim_vpi_handle_v1 value { };
    SystemVerilogVpiIteratorError error { SystemVerilogVpiIteratorError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiIteratorError::None && value != 0U;
    }
};

enum class SystemVerilogVpiObjectStateError {
    None,
    InvalidSimulation,
    InvalidState,
    MissingObject,
    TypeMismatch,
    ResourceLimit,
};

struct SystemVerilogVpiObjectState {
    fsim_vpi_handle_v1 source_handle { };
    std::string full_name;
    SystemVerilogVpiTypeInfo type;
    std::optional<SystemVerilogVpiStoredValue> value;
    std::optional<SystemVerilogVpiStoredValue> forced_value;
};

struct SystemVerilogVpiObjectStateSnapshot {
    std::uint64_t simulation_identity { };
    std::vector<SystemVerilogVpiObjectState> objects;
    SystemVerilogVpiObjectStateError error {
        SystemVerilogVpiObjectStateError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiObjectStateError::None;
    }
};

struct SystemVerilogVpiObjectHandleRemap {
    fsim_vpi_handle_v1 source { };
    fsim_vpi_handle_v1 target { };
};

struct SystemVerilogVpiObjectStateRestoreResult {
    std::vector<SystemVerilogVpiObjectHandleRemap> handles;
    SystemVerilogVpiObjectStateError error {
        SystemVerilogVpiObjectStateError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiObjectStateError::None;
    }
};

struct SystemVerilogVpiStoredValueLookupResult {
    std::optional<SystemVerilogVpiStoredValue> value;
    SystemVerilogVpiValueError error { SystemVerilogVpiValueError::None };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiValueError::None && value.has_value();
    }
};

class SystemVerilogVpiObjectRegistry final {
public:
    explicit SystemVerilogVpiObjectRegistry(
        std::uint64_t simulation_identity) noexcept;

    [[nodiscard]] std::uint64_t simulation_identity() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] SystemVerilogVpiObjectResult create(
        SystemVerilogVpiObjectKind kind,
        fsim_vpi_handle_v1 parent,
        std::string_view name);
    [[nodiscard]] SystemVerilogVpiObjectResult create(
        const SystemVerilogVpiObjectDescriptor& descriptor);
    [[nodiscard]] SystemVerilogVpiObjectLookupResult lookup(
        fsim_vpi_handle_v1 handle) const;
    [[nodiscard]] SystemVerilogVpiObjectLookupResult find(
        std::string_view full_name) const;
    [[nodiscard]] SystemVerilogVpiObjectLookupResult find_child(
        fsim_vpi_handle_v1 parent, std::string_view name) const;
    [[nodiscard]] SystemVerilogVpiTypeLookupResult type_info(
        fsim_vpi_handle_v1 handle) const;
    [[nodiscard]] static SystemVerilogVpiObjectCapabilities capabilities()
        noexcept;
    [[nodiscard]] static bool supports(
        SystemVerilogVpiObjectKind kind) noexcept;
    [[nodiscard]] static bool supports(
        SystemVerilogVpiRelationshipKind relationship) noexcept;
    [[nodiscard]] static bool supports(
        SystemVerilogVpiPropertyKind property) noexcept;
    [[nodiscard]] SystemVerilogVpiPropertyResult property(
        fsim_vpi_handle_v1 handle,
        SystemVerilogVpiPropertyKind property) const;
    [[nodiscard]] SystemVerilogVpiValueError bind_value(
        fsim_vpi_handle_v1 handle, SystemVerilogVpiStoredValue value);
    /// Publish a value supplied by the simulation kernel. This updates even a
    /// read-only object and preserves any VPI force as the effective value.
    [[nodiscard]] SystemVerilogVpiValueError update_bound_value(
        fsim_vpi_handle_v1 handle, SystemVerilogVpiStoredValue value);
    /// Publish a kernel-originated force without applying host write-access
    /// policy or feeding the mutation back into the kernel bridge.
    [[nodiscard]] SystemVerilogVpiValueError update_forced_value(
        fsim_vpi_handle_v1 handle, SystemVerilogVpiStoredValue value);
    /// Publish a kernel-originated release and reveal the retained stored value.
    [[nodiscard]] SystemVerilogVpiValueError release_bound_force(
        fsim_vpi_handle_v1 handle);
    [[nodiscard]] SystemVerilogVpiValueError deposit_value(
        fsim_vpi_handle_v1 handle, SystemVerilogVpiStoredValue value);
    [[nodiscard]] SystemVerilogVpiValueError force_value(
        fsim_vpi_handle_v1 handle, SystemVerilogVpiStoredValue value);
    [[nodiscard]] SystemVerilogVpiValueError release_forced_value(
        fsim_vpi_handle_v1 handle);
    [[nodiscard]] SystemVerilogVpiValueReadResult read_value(
        fsim_vpi_handle_v1 handle,
        SystemVerilogVpiValueFormat format,
        SystemVerilogVpiValueReadBuffers buffers = { }) const;
    /// Return one canonical value without converting through an external VPI
    /// buffer. Reader/history services use this per-object access to avoid a
    /// whole-registry snapshot.
    [[nodiscard]] SystemVerilogVpiStoredValueLookupResult stored_value(
        fsim_vpi_handle_v1 handle) const;
    [[nodiscard]] SystemVerilogVpiValueError reset_values();
    [[nodiscard]] SystemVerilogVpiObjectStateSnapshot snapshot_values() const;
    [[nodiscard]] SystemVerilogVpiObjectStateRestoreResult restore_values(
        const SystemVerilogVpiObjectStateSnapshot& snapshot);
    [[nodiscard]] SystemVerilogVpiValueError validate_value_write(
        fsim_vpi_handle_v1 handle,
        const SystemVerilogVpiStoredValue* value) const;
    [[nodiscard]] SystemVerilogVpiObjectError release(
        fsim_vpi_handle_v1 handle);
    [[nodiscard]] std::optional<std::uint64_t> add_value_observer(
        SystemVerilogVpiValueObserver observer);
    [[nodiscard]] bool remove_value_observer(
        std::uint64_t observer);
    [[nodiscard]] std::optional<std::uint64_t> add_value_state_observer(
        SystemVerilogVpiValueStateObserver observer);
    [[nodiscard]] bool remove_value_state_observer(
        std::uint64_t observer);

    [[nodiscard]] SystemVerilogVpiIteratorResult iterate_children(
        fsim_vpi_handle_v1 parent);
    [[nodiscard]] SystemVerilogVpiIteratorResult iterate_objects(
        SystemVerilogVpiObjectKind kind,
        fsim_vpi_handle_v1 parent = 0);
    [[nodiscard]] SystemVerilogVpiIteratorResult iterate_relationship(
        fsim_vpi_handle_v1 object,
        SystemVerilogVpiRelationshipKind relationship);
    [[nodiscard]] SystemVerilogVpiIteratorScanResult scan(
        fsim_vpi_handle_v1 iterator);
    [[nodiscard]] SystemVerilogVpiIteratorError release_iterator(
        fsim_vpi_handle_v1 iterator);

private:
    struct Record {
        std::uint16_t epoch { };
        bool live { };
        std::uint32_t live_children { };
        std::uint64_t ordinal { };
        fsim_vpi_handle_v1 parent { };
        SystemVerilogVpiObjectKind kind { SystemVerilogVpiObjectKind::Root };
        std::string name;
        std::string full_name;
        std::optional<SystemVerilogVpiSourceLocation> source;
        std::optional<SystemVerilogVpiTypeInfo> type;
        std::optional<SystemVerilogVpiStoredValue> value;
        std::optional<SystemVerilogVpiStoredValue> forced_value;
        std::optional<SystemVerilogVpiStoredValue> initial_value;
    };
    struct IteratorRecord {
        std::uint16_t epoch { };
        bool live { };
        std::size_t cursor { };
        std::vector<fsim_vpi_handle_v1> objects;
    };

    [[nodiscard]] fsim_vpi_handle_v1 encode_object(
        std::uint32_t slot, std::uint16_t epoch) const noexcept;
    [[nodiscard]] fsim_vpi_handle_v1 encode_iterator(
        std::uint32_t slot, std::uint16_t epoch) const noexcept;
    [[nodiscard]] SystemVerilogVpiObjectError resolve_object(
        fsim_vpi_handle_v1 handle, std::uint32_t& slot) const noexcept;
    [[nodiscard]] SystemVerilogVpiIteratorError resolve_iterator(
        fsim_vpi_handle_v1 handle, std::uint32_t& slot) const noexcept;
    [[nodiscard]] static bool normalize_name(
        std::string_view input,
        std::string& normalized,
        std::string& hierarchy_segment);
    [[nodiscard]] static std::string sibling_key(
        fsim_vpi_handle_v1 parent, std::string_view name);
    [[nodiscard]] SystemVerilogVpiObjectLookupResult lookup_locked(
        fsim_vpi_handle_v1 handle) const;
    [[nodiscard]] SystemVerilogVpiIteratorResult create_iterator_locked(
        std::vector<fsim_vpi_handle_v1> objects);

    std::uint64_t simulation_identity_ { };
    std::uint32_t registry_identity_ { };
    std::uint64_t next_ordinal_ { };
    mutable std::mutex mutex_;
    std::vector<Record> records_;
    std::vector<std::uint32_t> free_slots_;
    std::vector<IteratorRecord> iterators_;
    std::vector<std::uint32_t> free_iterators_;
    std::unordered_map<std::string, fsim_vpi_handle_v1> siblings_;
    std::unordered_map<std::string, fsim_vpi_handle_v1> full_names_;
    std::uint64_t next_value_observer_ { 1 };
    std::map<std::uint64_t, SystemVerilogVpiValueObserver>
        value_observers_;
    std::uint64_t next_value_state_observer_ { 1 };
    std::map<std::uint64_t, SystemVerilogVpiValueStateObserver>
        value_state_observers_;
};

} // namespace fsim::runtime
