// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_object.hpp"
#include "fsim/runtime/scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fsim::runtime {

/// Stable reader object identities used by the SystemVerilog data-read API.
enum class SystemVerilogVpiDataReadObjectKind : std::int32_t {
    TraverseObject = 800,
    Collection = 810,
    ObjectCollection = 811,
    TraverseCollection = 812,
};

enum class SystemVerilogVpiDataReadProperty : std::int32_t {
    IsLoaded = 820,
    HasDataValueChange = 821,
    HasValueChange = 822,
    HasNoValue = 823,
    BelongsToExtension = 824,
};

enum class SystemVerilogVpiDataReadAccess : std::int32_t {
    LimitedInteractive = 830,
    Interactive = 831,
    PostProcess = 832,
};

enum class SystemVerilogVpiDataReadIteration : std::int32_t {
    DataLoaded = 850,
};

enum class SystemVerilogVpiDataReadControl : std::int32_t {
    MinimumTime = 860,
    MaximumTime = 864,
    PreviousValueChange = 868,
    NextValueChange = 870,
    Time = 874,
};

enum class SystemVerilogVpiDataReadError {
    None,
    InvalidSimulation,
    InvalidAccess,
    InvalidDatabase,
    InvalidHandle,
    CrossExtension,
    ReleasedHandle,
    InvalidObject,
    InvalidCollection,
    InvalidProperty,
    InvalidControl,
    NotLoaded,
    NoValue,
    NoValueChange,
    Closed,
    OutOfOrder,
    TypeMismatch,
    ResourceLimit,
};

struct SystemVerilogVpiDataReadPosition {
    SimulationTick time { };
    std::uint64_t delta { };

    friend auto operator<=>(
        const SystemVerilogVpiDataReadPosition&,
        const SystemVerilogVpiDataReadPosition&) = default;
};

struct SystemVerilogVpiDataReadHandle {
    std::uint64_t owner { };
    std::uint64_t id { };
    SystemVerilogVpiDataReadObjectKind kind {
        SystemVerilogVpiDataReadObjectKind::TraverseObject
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return owner != 0U && id != 0U;
    }

    friend bool operator==(
        const SystemVerilogVpiDataReadHandle&,
        const SystemVerilogVpiDataReadHandle&) = default;
};

struct SystemVerilogVpiDataReadHandleResult {
    SystemVerilogVpiDataReadHandle value;
    SystemVerilogVpiDataReadError error {
        SystemVerilogVpiDataReadError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiDataReadError::None
            && static_cast<bool>(value);
    }
};

struct SystemVerilogVpiDataReadStatusResult {
    bool value { };
    SystemVerilogVpiDataReadError error {
        SystemVerilogVpiDataReadError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiDataReadError::None;
    }
};

struct SystemVerilogVpiDataReadTimeResult {
    SystemVerilogVpiDataReadPosition value;
    SystemVerilogVpiDataReadError error {
        SystemVerilogVpiDataReadError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiDataReadError::None;
    }
};

struct SystemVerilogVpiDataReadValueResult {
    std::optional<SystemVerilogVpiStoredValue> value;
    SystemVerilogVpiDataReadError error {
        SystemVerilogVpiDataReadError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiDataReadError::None
            && value.has_value();
    }
};

struct SystemVerilogVpiDataReadMembersResult {
    std::vector<fsim_vpi_handle_v1> objects;
    std::vector<SystemVerilogVpiDataReadHandle> traverses;
    SystemVerilogVpiDataReadError error {
        SystemVerilogVpiDataReadError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiDataReadError::None;
    }
};

struct SystemVerilogVpiDataReadLimits {
    std::size_t maximum_loaded_objects { 1U << 20U };
    std::size_t maximum_value_changes { 1U << 22U };
    std::size_t maximum_collections { 1U << 18U };
    std::size_t maximum_collection_members { 1U << 20U };
    std::size_t maximum_traverse_objects { 1U << 20U };
    std::size_t maximum_database_name_bytes { 1U << 20U };
};

struct SystemVerilogVpiDataReadFilter {
    std::optional<SystemVerilogVpiObjectKind> object_kind;
    std::optional<SystemVerilogVpiDataReadProperty> property;
    bool match { true };
};

/// Simulation-owned implementation of the standardized reader model. Design
/// handles remain owned by SystemVerilogVpiObjectRegistry; this service stores
/// only load selections, bounded value-change history, and reader handles.
class SystemVerilogVpiDataReadService final {
public:
    using Clock = std::function<SystemVerilogVpiDataReadPosition()>;

    SystemVerilogVpiDataReadService(
        SystemVerilogVpiObjectRegistry& objects,
        SystemVerilogVpiDataReadAccess access,
        std::string database_name = { },
        Clock clock = { },
        SystemVerilogVpiDataReadLimits limits = { });
    ~SystemVerilogVpiDataReadService();

    SystemVerilogVpiDataReadService(
        const SystemVerilogVpiDataReadService&) = delete;
    SystemVerilogVpiDataReadService& operator=(
        const SystemVerilogVpiDataReadService&) = delete;
    SystemVerilogVpiDataReadService(
        SystemVerilogVpiDataReadService&&) = delete;
    SystemVerilogVpiDataReadService& operator=(
        SystemVerilogVpiDataReadService&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] std::uint64_t extension_identity() const noexcept;
    [[nodiscard]] SystemVerilogVpiDataReadAccess access() const noexcept;
    [[nodiscard]] std::string database_name() const;

    [[nodiscard]] SystemVerilogVpiDataReadError close();
    [[nodiscard]] SystemVerilogVpiDataReadError load_init(
        std::optional<SystemVerilogVpiDataReadHandle> collection,
        std::optional<fsim_vpi_handle_v1> scope,
        std::uint32_t levels = 0U);
    [[nodiscard]] SystemVerilogVpiDataReadError load(
        fsim_vpi_handle_v1 object);
    [[nodiscard]] SystemVerilogVpiDataReadError load(
        SystemVerilogVpiDataReadHandle collection);
    [[nodiscard]] SystemVerilogVpiDataReadError unload(
        fsim_vpi_handle_v1 object);
    [[nodiscard]] SystemVerilogVpiDataReadError unload(
        SystemVerilogVpiDataReadHandle collection);

    [[nodiscard]] SystemVerilogVpiDataReadHandleResult create_object_collection(
        std::optional<SystemVerilogVpiDataReadHandle> collection = std::nullopt,
        std::optional<fsim_vpi_handle_v1> object = std::nullopt);
    [[nodiscard]] SystemVerilogVpiDataReadHandleResult create_traverse(
        fsim_vpi_handle_v1 object);
    [[nodiscard]] SystemVerilogVpiDataReadHandleResult
    create_traverse_collection(
        std::optional<SystemVerilogVpiDataReadHandle> collection = std::nullopt,
        std::optional<SystemVerilogVpiDataReadHandle> traverse = std::nullopt);
    [[nodiscard]] SystemVerilogVpiDataReadHandleResult filter(
        SystemVerilogVpiDataReadHandle collection,
        const SystemVerilogVpiDataReadFilter& filter);
    [[nodiscard]] SystemVerilogVpiDataReadHandleResult go_to(
        SystemVerilogVpiDataReadHandle traverse,
        SystemVerilogVpiDataReadControl control,
        std::optional<SystemVerilogVpiDataReadPosition> time = std::nullopt);

    [[nodiscard]] SystemVerilogVpiDataReadStatusResult property(
        SystemVerilogVpiDataReadProperty property,
        fsim_vpi_handle_v1 object) const;
    [[nodiscard]] SystemVerilogVpiDataReadStatusResult property(
        SystemVerilogVpiDataReadProperty property,
        SystemVerilogVpiDataReadHandle object) const;
    [[nodiscard]] SystemVerilogVpiDataReadTimeResult time(
        SystemVerilogVpiDataReadHandle traverse,
        SystemVerilogVpiDataReadControl selection
            = SystemVerilogVpiDataReadControl::Time) const;
    [[nodiscard]] SystemVerilogVpiDataReadValueResult value(
        SystemVerilogVpiDataReadHandle traverse) const;
    [[nodiscard]] SystemVerilogVpiDataReadMembersResult members(
        SystemVerilogVpiDataReadHandle collection) const;
    [[nodiscard]] SystemVerilogVpiDataReadMembersResult loaded_objects(
        std::optional<fsim_vpi_handle_v1> scope = std::nullopt) const;
    [[nodiscard]] SystemVerilogVpiDataReadError release(
        SystemVerilogVpiDataReadHandle object);

    /// Publish an independently decoded database sample or a kernel sample.
    /// Input is validated and appended atomically in time/delta order.
    [[nodiscard]] SystemVerilogVpiDataReadError publish_value_change(
        fsim_vpi_handle_v1 object,
        SystemVerilogVpiDataReadPosition position,
        SystemVerilogVpiStoredValue value);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace fsim::runtime
