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

enum class SystemVerilogVpiObjectKind {
    Root,
    Module,
    Interface,
    Program,
    Package,
    GenerateScope,
    Port,
    Net,
    Variable,
    Parameter,
    Memory,
    Array,
    Class,
    ClassProperty,
    NamedEvent,
    /// Executable occurrence published beneath its owning hierarchy scope.
    Process,
    /// Concurrent assertion occurrence with stable hierarchy identity.
    Assertion,
    /// Read-only stored contribution, distinct from a signal's effective value.
    Driver,
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
    ResourceLimit,
};

enum class SystemVerilogVpiIteratorError {
    None,
    InvalidSimulation,
    InvalidObject,
    InvalidHandle,
    CrossSimulation,
    StaleHandle,
    ReleasedHandle,
    End,
    ResourceLimit,
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
