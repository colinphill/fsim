// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc_abi.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime::simir {
class ProcessExecutionContext;
}

namespace fsim::systemc {

struct PortDescription {
    fsim_sc_handle_v1 handle{};
    std::string name;
    fsim_sc_port_direction_v1 direction{FSIM_SC_INPUT};
    fsim_sc_value_encoding_v1 encoding{FSIM_SC_BIT2};
    std::uint32_t width{};
    fsim_sc_handle_v1 bound_object{};
};

struct ForeignPortDescription {
    std::string name;
    fsim_sc_port_direction_v1 direction{FSIM_SC_INPUT};
    fsim_sc_value_encoding_v1 encoding{FSIM_SC_BIT2};
    std::uint32_t width{};
    fsim_sc_handle_v1 object{};
    fsim_sc_handle_v1 handle{};
};

struct ForeignChildDescription {
    fsim_sc_handle_v1 handle{};
    std::string name;
    std::vector<std::pair<std::string, std::int64_t>>
        construction_actuals;
    std::vector<ForeignPortDescription> ports;
    bool module_facade{};
};

struct ConstructionParameterDescription {
    std::string name;
    fsim_sc_construction_type_v1 type{
        FSIM_SC_CONSTRUCTION_INTEGER};
    std::optional<std::int64_t> default_value;
};

struct SensitivityDescription {
    fsim_sc_handle_v1 object{};
    fsim_sc_edge_kind_v1 edge{FSIM_SC_ANY_EDGE};
};

struct ProcessDescription {
    fsim_sc_handle_v1 handle{};
    std::string name;
    fsim_sc_process_kind_v1 kind{FSIM_SC_METHOD};
    fsim_sc_process_entry_v1 entry{};
    void* user{};
    std::vector<SensitivityDescription> sensitivity;
    bool initialize{true};
};

struct EventDescription {
    fsim_sc_handle_v1 handle{};
    std::string name;
};

struct PrimitiveChannelDescription {
    fsim_sc_handle_v1 handle{};
    std::string name;
    fsim_sc_channel_update_v1 update{};
    void* user{};
    std::string kind{"sc_prim_channel"};
};

struct MetadataObjectDescription {
    fsim_sc_handle_v1 handle{};
    std::string name;
    fsim_sc_metadata_category_v1 category{FSIM_SC_METADATA_PORT};
    std::string kind;
};

struct InternalSignalDescription {
    fsim_sc_handle_v1 handle{};
    std::string name;
    fsim_sc_value_encoding_v1 encoding{FSIM_SC_BIT2};
    std::uint32_t width{};
    std::vector<std::uint8_t> initial_value;
};

struct ExportDescription {
    fsim_sc_handle_v1 handle{};
    std::string name;
    fsim_sc_value_encoding_v1 encoding{FSIM_SC_BIT2};
    std::uint32_t width{};
    fsim_sc_handle_v1 bound_object{};
    bool writable{true};
};

struct LifecycleDescription {
    fsim_sc_lifecycle_entry_v1 before_end_of_elaboration{};
    fsim_sc_lifecycle_entry_v1 end_of_elaboration{};
    fsim_sc_lifecycle_entry_v1 start_of_simulation{};
    fsim_sc_lifecycle_entry_v1 end_of_simulation{};
    void* user{};
};

struct ModuleDescription {
    fsim_sc_handle_v1 handle{};
    fsim_sc_handle_v1 parent{};
    std::string factory;
    std::string instance;
    std::vector<std::pair<std::string, std::int64_t>>
        construction_values;
    std::vector<PortDescription> ports;
    std::vector<ForeignChildDescription> foreign_children;
    std::vector<ProcessDescription> processes;
    std::vector<EventDescription> events;
    std::vector<PrimitiveChannelDescription> primitive_channels;
    std::vector<InternalSignalDescription> internal_signals;
    std::vector<ExportDescription> exports;
    std::vector<MetadataObjectDescription> metadata_objects;
    std::vector<ModuleDescription> native_children;
    LifecycleDescription lifecycle;
};

enum class HierarchyObjectKind : std::uint8_t {
    module,
    port,
    foreign_child,
    process,
    event,
    primitive_channel,
    signal,
    export_object,
};

struct HierarchyObjectInfo {
    fsim_sc_handle_v1 handle{};
    fsim_sc_handle_v1 parent{};
    std::string name;
    std::string path;
    HierarchyObjectKind kind{HierarchyObjectKind::module};
};

enum class MethodSuspendKind : std::uint8_t {
    halt,
    static_sensitivity,
    wait_for,
    wait_event,
};

struct MethodSuspendResult {
    MethodSuspendKind kind{MethodSuspendKind::halt};
    std::uint64_t delay_ticks{};
    std::vector<std::uint32_t> event_signals;
    bool wait_all{};
    std::optional<std::uint64_t> timeout_ticks;
};

/// Owns one loaded SystemC plug-in and every module object constructed from
/// its append-only elaboration factory ABI.
///
/// The resulting descriptions contain no C++ or plug-in-owned layout and can
/// be copied into DesignIR. Module objects stay alive until this registry is
/// destroyed, before the dynamic library is unloaded.
class HierarchyRegistry final {
public:
    struct Impl;

    HierarchyRegistry(HierarchyRegistry&&) noexcept;
    HierarchyRegistry& operator=(HierarchyRegistry&&) noexcept;
    HierarchyRegistry(const HierarchyRegistry&) = delete;
    HierarchyRegistry& operator=(const HierarchyRegistry&) = delete;
    ~HierarchyRegistry();

    [[nodiscard]] static std::unique_ptr<HierarchyRegistry> load(
        const std::filesystem::path& path,
        std::string& error);

    [[nodiscard]] bool has_factory(std::string_view name) const noexcept;
    [[nodiscard]] bool has_elaboration_factory(
        std::string_view name) const noexcept;
    [[nodiscard]] std::size_t factory_count() const noexcept;

    /// Return copied, ABI-neutral metadata for one live hierarchy object.
    [[nodiscard]] std::optional<HierarchyObjectInfo> object_info(
        fsim_sc_handle_v1 handle) const;

    /// Return direct children in stable registration-handle order.
    [[nodiscard]] std::vector<HierarchyObjectInfo> child_objects(
        fsim_sc_handle_v1 parent) const;

    /// Find an absolute or root-relative path within one live factory root.
    [[nodiscard]] std::optional<HierarchyObjectInfo> find_object(
        fsim_sc_handle_v1 root,
        std::string_view path) const;
    [[nodiscard]] std::optional<
        std::vector<ConstructionParameterDescription>>
    factory_parameters(std::string_view name) const;

    [[nodiscard]] std::optional<ModuleDescription> instantiate(
        std::string_view factory,
        std::string_view instance,
        fsim_sc_handle_v1 parent,
        std::string& error);
    [[nodiscard]] std::optional<ModuleDescription> instantiate(
        std::string_view factory,
        std::string_view instance,
        fsim_sc_handle_v1 parent,
        std::span<const std::pair<std::string, std::int64_t>>
            construction_actuals,
        std::string& error);

    /// Bind one plug-in object handle to its dense common-runtime signal.
    /// Bindings are immutable and may be shared by repeated simulations of
    /// the same built design.
    void bind_runtime_object(
        fsim_sc_handle_v1 object,
        std::uint32_t signal);

    /// Set the exact number of femtoseconds represented by one common
    /// simulation tick. Must be called before process execution.
    void set_time_resolution(std::uint64_t femtoseconds_per_tick);

    /// Invoke before_end_of_elaboration for every selected root, then
    /// end_of_elaboration for every root. Each phase runs at most once.
    void complete_elaboration(
        std::span<const fsim_sc_handle_v1> roots);

    /// Invoke start_of_simulation once for every selected root.
    void start_simulation(
        std::span<const fsim_sc_handle_v1> roots);

    /// Invoke end_of_simulation once in reverse root order.
    void end_simulation(
        std::span<const fsim_sc_handle_v1> roots);

    /// Invoke or resume a registered SystemC process with host reads/writes
    /// redirected to the supplied common-kernel execution context.
    [[nodiscard]] MethodSuspendResult invoke_process(
        fsim_sc_handle_v1 process,
        runtime::simir::ProcessExecutionContext& context);

    /// Invoke a registered primitive-channel update inside the supplied
    /// common-kernel update-phase context.
    void invoke_primitive_channel(
        fsim_sc_handle_v1 channel,
        runtime::simir::ProcessExecutionContext& context);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    explicit HierarchyRegistry(std::unique_ptr<Impl> impl) noexcept;
    void reset() noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::systemc
