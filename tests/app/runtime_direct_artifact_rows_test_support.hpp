// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../../src/app/application_design_artifact_codec_internal.hpp"
#include "../../src/app/application_runtime_path_codec.hpp"
#include "../../src/elaboration/elaborated_design_process_access.hpp"
#include "../runtime/runtime_owned_driver_demotion_test_access.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_operation_storage.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::app::direct_artifact_rows_test {

using elaboration::ElaboratedDesign;
using elaboration::ElaboratedDesignState;
using runtime::Logic4;
using runtime::PackedLogic4;
using runtime::simir::DebugLocal;
using runtime::simir::DebugPoint;
using runtime::simir::DebugPointKind;
using runtime::simir::EdgeKind;
using runtime::simir::Halt;
using runtime::simir::LoadConstant;
using runtime::simir::Process;
using runtime::simir::ProcessSchedulingDomain;
using runtime::simir::ProcessStartupPhase;
using runtime::simir::ReadSignal;
using runtime::simir::SignalId;
using runtime::simir::ValueKind;
using runtime::simir::WriteUpdate;

inline Process make_copy_process(const runtime::simir::ProcessId id,
    std::string name, const SignalId input,
    const SignalId output,
    const std::string_view debug_scope)
{
    Process process;
    process.id = id;
    process.name = std::move(name);
    process.language_standard = "1800-2017";
    process.compatibility_profile = "none";
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    DebugLocal local;
    local.name = "sample";
    local.type_name = "logic";
    local.register_id = 0U;
    local.width = 1U;
    local.value_kind = ValueKind::logic4;
    process.debug_locals.push_back(std::move(local));
    process.static_sensitivity = { { input, EdgeKind::any, 0U, 1U } };
    process.driver_regions = { { output, 0U, 1U, true } };
    process.operations.emplace_back(ReadSignal { 0U, input });
    process.operations.emplace_back(WriteUpdate { output, 0U });
    process.operations.emplace_back(
        DebugPoint { DebugPointKind::statement,
            runtime::simir::SourceLocation { "direct_rows.sv", 7U, 3U },
            std::string { debug_scope } });
    process.operations.emplace_back(Halt { });
    return process;
}

inline ElaboratedDesignState make_state()
{
    ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };

    const auto add_signal = [&](const std::string_view name,
                                const Logic4 initial) {
        const auto id = static_cast<SignalId>(state.signals.size());
        const std::string path = "top." + std::string { name };
        elaboration::SignalInfo info;
        info.id = id;
        info.name = path;
        info.width = 1U;
        info.type_name = "logic";
        info.source_domain = frontend::ValueDomain::Logic4;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(path, PackedLogic4 { 1U, initial });
        state.signal_names.emplace_back(path, id);
        return id;
    };

    const auto input_a = add_signal("input_a", Logic4::one);
    const auto input_b = add_signal("input_b", Logic4::zero);
    const auto output_a = add_signal("output_a", Logic4::zero);
    const auto output_b = add_signal("output_b", Logic4::zero);
    const auto output_c = add_signal("output_c", Logic4::zero);

    auto first = make_copy_process(0U, "top.first", input_a, output_a, "top.first_scope");
    auto second = make_copy_process(1U, "top.second", input_a, output_a,
        "top.second_scope");
    second.static_sensitivity = { { input_b, EdgeKind::any, 0U, 1U } };
    second.driver_regions = { { output_b, 0U, 1U, true } };
    auto second_read = second.operations.expanded(0U);
    runtime::simir::operation_get_if<ReadSignal>(&second_read)->signal = input_b;
    second.operations.replace(0U, std::move(second_read));
    auto second_write = second.operations.expanded(1U);
    runtime::simir::operation_get_if<WriteUpdate>(&second_write)->signal = output_b;
    second.operations.replace(1U, std::move(second_write));
    auto second_debug = second.operations.expanded(2U);
    runtime::simir::operation_get_if<DebugPoint>(&second_debug)->scope = "top.second_scope";
    second.operations.replace(2U, std::move(second_debug));
    assert(
        runtime::simir::share_process_operations(first, second, state.signals));

    Process independent;
    independent.id = 2U;
    independent.name = "top.independent";
    independent.language_standard = "1800-2017";
    independent.compatibility_profile = "none";
    independent.register_count = 4U;
    independent.string_register_count = 1U;
    independent.register_value_kinds = { ValueKind::logic4, ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4 };
    DebugLocal independent_local;
    independent_local.name = "independent_sample";
    independent_local.type_name = "logic";
    independent_local.register_id = 0U;
    independent_local.width = 1U;
    independent_local.value_kind = ValueKind::logic4;
    independent.debug_locals.push_back(std::move(independent_local));
    independent.driver_regions = { { output_c, 0U, 1U, true } };
    independent.operations.emplace_back(
        LoadConstant { 0U, PackedLogic4 { 1U, Logic4::one } });
    independent.operations.emplace_back(
        LoadConstant { 1U, PackedLogic4 { 1U, Logic4::zero } });
    independent.operations.emplace_back(WriteUpdate { output_c, 0U });
    independent.operations.emplace_back(
        DebugPoint { DebugPointKind::statement,
            runtime::simir::SourceLocation { "direct_rows.sv", 15U, 3U },
            "top.independent_scope" });
    independent.operations.emplace_back(
        runtime::simir::LoadStringConstant { 0U, "4.5" });
    independent.operations.emplace_back(runtime::simir::FileScan {
        3U, 0U, 0U, true,
        { { "", runtime::simir::InputScanFormat::time, 0U, false,
            { runtime::simir::InputScanTargetKind::packed_register,
                2U, 64U, true, runtime::SystemVerilogScalarKind::Real } } },
        { }, false, std::nullopt, false, 1'000'000U });
    independent.operations.emplace_back(Halt { });

    state.processes.push_back(std::move(first));
    state.processes.push_back(std::move(second));
    state.processes.push_back(std::move(independent));
    return state;
}

inline std::optional<ElaboratedDesign> make_source_design()
{
    return ElaboratedDesign::from_state(make_state());
}

inline std::optional<std::string> encode_source_design()
{
    auto design = make_source_design();
    assert(design.has_value());
    diagnostic::Engine diagnostics;
    auto bytes = serialize_runtime_state(*design, diagnostics);
    assert(bytes.has_value() && !diagnostics.has_error());
    return bytes;
}

inline ElaboratedDesignState make_startup_metadata_state()
{
    auto state = make_state();
    state.processes[0U].startup_phase
        = ProcessStartupPhase::package_static_initializer;
    state.processes[0U].scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    state.processes[0U].static_sensitivity.clear();
    state.processes[0U].startup_order = 0xfedcba9876543210ULL;
    state.processes[0U].startup_package_owner_id = 8U;
    fsim::elaboration::StringObjectInfo string_info;
    string_info.id = 0U;
    string_info.name = "top.startup_string";
    state.string_object_info.push_back(string_info);
    state.string_objects.push_back(
        { "top.startup_string", "startup value", 8U });
    state.string_names.emplace_back("top.startup_string", 0U);

    fsim::runtime::simir::ContainerType container_type;
    container_type.fixed = true;
    container_type.index_left = 0;
    container_type.index_right = 0;
    container_type.dimensions = { { 0, 0 } };
    fsim::elaboration::ContainerObjectInfo container_info;
    container_info.id = 0U;
    container_info.name = "top.startup_container";
    container_info.type = container_type;
    state.container_object_info.push_back(container_info);
    state.container_objects.push_back({ "top.startup_container",
        { container_type,
            { fsim::runtime::PackedLogic4 { 1U, Logic4::zero } }, { } },
        std::nullopt, 8U });
    state.container_names.emplace_back("top.startup_container", 0U);

    auto& startup = state.processes[0U];
    startup.string_register_count = 1U;
    startup.container_register_count = 1U;
    startup.container_register_types.push_back(container_type);
    startup.startup_writable_string_object_ids = { 0U };
    startup.startup_writable_container_object_ids = { 0U };
    startup.operations = {
        ReadSignal { 0U, 0U },
        WriteUpdate { 2U, 0U },
        runtime::simir::DebugPoint {
            runtime::simir::DebugPointKind::statement,
            runtime::simir::SourceLocation {
                "direct_rows.sv", 7U, 3U },
            "top.first_scope" },
        runtime::simir::LoadStringConstant { 0U, "roundtrip text" },
        runtime::simir::WriteStringObject { 0U, 0U },
        runtime::simir::ReadContainerObject { 0U, 0U },
        runtime::simir::WriteContainerObject {
            0U, 0U, std::nullopt, false },
        Halt { },
    };

    elaboration::SystemVerilogPackageClassInitializerInfo handle;
    handle.unit = fsim::semantic::UnitId::from_index(7U);
    handle.declaration = fsim::semantic::DeclarationId::from_index(11U);
    handle.signal = 1U;
    handle.initializer = fsim::semantic::ExpressionId::from_index(21U);
    handle.startup_order = 41U;
    handle.writable_signal_ids = { 0U, 1U, 4U };
    handle.writable_string_object_ids = { 0U };
    handle.writable_container_object_ids = { 0U };
    state.systemverilog_package_class_initializers.push_back(handle);

    elaboration::SystemVerilogPackageClassStaticInitializerInfo property;
    property.unit = fsim::semantic::UnitId::from_index(8U);
    property.class_identity = "work::startup_pkg::owner";
    property.specialization_identity = "work::startup_pkg::owner<>";
    property.property_declaration
        = fsim::semantic::DeclarationId::from_index(12U);
    property.property_name = "item";
    property.canonical_identity =
        "work::startup_pkg::owner::item";
    property.initializer = fsim::semantic::ExpressionId::from_index(22U);
    property.startup_order = 42U;
    property.writable_signal_ids = { 2U, 4U };
    property.writable_string_object_ids = { 0U };
    property.writable_container_object_ids = { 0U };
    state.systemverilog_package_class_static_initializers.push_back(
        std::move(property));
    return state;
}

inline std::optional<std::string> encode_startup_metadata_design()
{
    auto state = make_startup_metadata_state();
    auto design = ElaboratedDesign::from_state(std::move(state));
    assert(design.has_value());
    diagnostic::Engine diagnostics;
    auto bytes = serialize_runtime_state(*design, diagnostics);
    assert(bytes.has_value() && !diagnostics.has_error());
    return bytes;
}

// Read and rewrite the runtime envelope by semantic fields so corruption
// tests can preserve its hierarchy binding without depending on row offsets.
struct RuntimeWirePayload {
    std::uint32_t schema { };
    std::uint8_t path_mode { };
    std::string path_binding;
    std::vector<std::uint32_t> path_ids;
    ElaboratedDesignState state;
    std::shared_ptr<elaboration::detail::RuntimeProcessProgramTable>
        process_rows;
};

inline std::optional<RuntimeWirePayload> read_runtime_wire_payload(
    const std::string_view bytes)
{
    codec_detail::Reader reader { bytes };
    reader.set_runtime_operation_body_sharing(true);
    reader.set_runtime_process_layout_sharing(true);
    reader.set_runtime_string_table(true);
    RuntimeWirePayload payload;
    if (!reader.raw("FSIMRUN1") || !reader.read(payload.schema)
        || !reader.read(payload.path_mode)
        || !reader.read(payload.path_binding)
        || !reader.read(payload.path_ids)
        || !reader.read(payload.state)
        || !reader.read(payload.process_rows)
        || reader.remaining() != 0U || !reader.failure().empty()) {
        return std::nullopt;
    }
    return payload;
}

inline std::optional<std::string> write_runtime_wire_payload(
    const RuntimeWirePayload& payload)
{
    codec_detail::Writer writer;
    writer.set_runtime_path_projection(true);
    writer.set_runtime_operation_body_sharing(true);
    writer.set_runtime_process_layout_sharing(true);
    writer.set_runtime_string_table(true);
    writer.raw("FSIMRUN1");
    writer.write(payload.schema);
    writer.write(payload.path_mode);
    writer.write(payload.path_binding);
    writer.write(payload.path_ids);
    writer.write(payload.state);
    writer.write(payload.process_rows);
    if (!writer.failure().empty()) {
        return std::nullopt;
    }
    return std::move(writer).finish();
}

template <typename Mutator>
inline std::optional<std::string> encode_malformed_process_rows(
    const std::string_view valid_bytes, Mutator&& mutate)
{
    auto payload = read_runtime_wire_payload(valid_bytes);
    if (!payload || !payload->process_rows) {
        return std::nullopt;
    }
    payload->process_rows
        = std::make_shared<elaboration::detail::RuntimeProcessProgramTable>(
            *payload->process_rows);
    std::invoke(std::forward<Mutator>(mutate), *payload->process_rows);
    return write_runtime_wire_payload(*payload);
}

inline bool decode_malformed_process_rows(const std::string_view bytes,
    const std::string_view source_name,
    const std::string_view expected_message)
{
    diagnostic::Engine diagnostics;
    const auto decoded
        = runtime_path_codec::deserialize_runtime_program_state(
            bytes, std::string { source_name }, nullptr, diagnostics);
    return !decoded && diagnostics.has_error()
        && std::ranges::any_of(diagnostics.diagnostics(),
            [&](const auto& diagnostic) {
                return diagnostic.message.find(expected_message)
                    != std::string::npos;
            });
}

inline std::optional<ElaboratedDesign>
decode_design(const std::string_view bytes)
{
    diagnostic::Engine diagnostics;
    auto design = deserialize_runtime_state(bytes, "direct-artifact-rows-test",
        diagnostics);
    assert(design.has_value() && !diagnostics.has_error());
    return design;
}

inline void assert_view_rows(const ElaboratedDesign& design)
{
    using Access = elaboration::detail::ElaboratedDesignProcessAccess;
    using runtime::simir::operation_get_if;
    assert(Access::row_backed(design));
    assert(Access::process_count(design) == 3U);
    const auto table = Access::process_table(design);
    assert(table != nullptr);
    assert(table->rows.size() == 3U);

    const auto first = Access::process_view(design, 0U);
    const auto second = Access::process_view(design, 1U);
    const auto independent = Access::process_view(design, 2U);
    assert(first.valid() && second.valid() && independent.valid());
    const bool package_startup
        = first.startup_phase()
        == ProcessStartupPhase::package_static_initializer;
    if (package_startup) {
        assert(table->templates.size() == 3U);
        assert(table->rows[0U].template_id == 0U);
        assert(table->rows[1U].template_id == 1U);
        assert(table->rows[2U].template_id == 2U);
        assert(first.scheduling_domain()
            == ProcessSchedulingDomain::systemverilog);
        assert(first.static_sensitivity().empty());
        assert(first.common_identity() != second.common_identity());
    } else {
        assert(first.startup_phase() == ProcessStartupPhase::ordinary);
        assert(table->templates.size() == 2U);
        assert(table->rows[0U].template_id == 0U);
        assert(table->rows[1U].template_id == 0U);
        assert(table->rows[2U].template_id == 1U);
        assert(first.scheduling_domain() == ProcessSchedulingDomain::generic);
        assert(first.static_sensitivity().size() == 1U);
        assert(first.static_sensitivity().front().signal == 0U);
        assert(first.common_identity() == second.common_identity());
    }
    assert(second.scheduling_domain() == ProcessSchedulingDomain::generic);
    assert(independent.scheduling_domain() == ProcessSchedulingDomain::generic);
    assert(second.common_identity() != independent.common_identity());
    assert(first.common_identity() != independent.common_identity());
    if (package_startup) {
        assert(first.operations().body_identity()
            != second.operations().body_identity());
    } else {
        assert(first.operations().body_identity()
            == second.operations().body_identity());
    }
    assert(first.name() == "top.first");
    assert(second.name() == "top.second");
    assert(independent.name() == "top.independent");
    assert(first.register_count() == 1U && second.register_count() == 1U);
    assert(independent.register_count() == 4U);
    assert(first.debug_locals().size() == 1U);
    assert(first.debug_locals()[0U].name == "sample");
    assert(independent.debug_locals()[0U].name == "independent_sample");
    assert(second.static_sensitivity().size() == 1U);
    assert(second.static_sensitivity().front().signal == 1U);
    assert(first.driver_regions().front().signal == 2U);
    assert(second.driver_regions().front().signal == 3U);
    assert(independent.driver_regions().front().signal == 4U);

    auto first_read_operation = first.operations().expanded(0U);
    const auto* first_read = operation_get_if<ReadSignal>(&first_read_operation);
    assert(first_read != nullptr && first_read->signal == 0U);
    auto second_read_operation = second.operations().expanded(0U);
    const auto* second_read = operation_get_if<ReadSignal>(&second_read_operation);
    assert(second_read != nullptr && second_read->signal == 1U);
    auto first_write_operation = first.operations().expanded(1U);
    const auto* first_write = operation_get_if<WriteUpdate>(&first_write_operation);
    assert(first_write != nullptr && first_write->signal == 2U);
    auto second_write_operation = second.operations().expanded(1U);
    const auto* second_write = operation_get_if<WriteUpdate>(&second_write_operation);
    assert(second_write != nullptr && second_write->signal == 3U);
    auto first_debug_operation = first.operations().expanded(2U);
    const auto* first_debug = operation_get_if<DebugPoint>(&first_debug_operation);
    assert(first_debug != nullptr && first_debug->scope == "top.first_scope");
    auto second_debug_operation = second.operations().expanded(2U);
    const auto* second_debug = operation_get_if<DebugPoint>(&second_debug_operation);
    assert(second_debug != nullptr && second_debug->scope == "top.second_scope");
}

inline void assert_facade_rows(const std::vector<Process>& processes)
{
    using runtime::simir::operation_get_if;
    assert(processes.size() == 3U);
    assert(processes[0U].id == 0U && processes[0U].name == "top.first");
    assert(processes[1U].id == 1U && processes[1U].name == "top.second");
    assert(processes[2U].id == 2U && processes[2U].name == "top.independent");
    assert(processes[0U].operations.shares_body_with(processes[1U].operations));
    assert(!processes[0U].operations.shares_body_with(processes[2U].operations));
    assert(processes[0U].static_sensitivity.front().signal == 0U);
    assert(processes[1U].static_sensitivity.front().signal == 1U);
    assert(processes[0U].driver_regions.front().signal == 2U);
    assert(processes[1U].driver_regions.front().signal == 3U);
    assert(processes[2U].driver_regions.front().signal == 4U);
    assert(processes[0U].debug_locals.front().name == "sample");
    assert(processes[1U].debug_locals.front().name == "sample");
    assert(processes[2U].debug_locals.front().name == "independent_sample");
    auto second_read_operation = processes[1U].operations.expanded(0U);
    const auto* second_read = operation_get_if<ReadSignal>(&second_read_operation);
    assert(second_read != nullptr && second_read->signal == 1U);
    auto second_write_operation = processes[1U].operations.expanded(1U);
    const auto* second_write = operation_get_if<WriteUpdate>(&second_write_operation);
    assert(second_write != nullptr && second_write->signal == 3U);
    auto second_debug_operation = processes[1U].operations.expanded(2U);
    const auto* second_debug = operation_get_if<DebugPoint>(&second_debug_operation);
    assert(second_debug != nullptr && second_debug->scope == "top.second_scope");
}

inline void assert_startup_metadata_view_rows(const ElaboratedDesign& design)
{
    using Access = elaboration::detail::ElaboratedDesignProcessAccess;
    using runtime::simir::operation_get_if;
    const auto first = Access::process_view(design, 0U);
    const auto second = Access::process_view(design, 1U);
    const auto independent = Access::process_view(design, 2U);
    assert(first.startup_phase()
        == ProcessStartupPhase::package_static_initializer);
    assert(first.startup_order() == 0xfedcba9876543210ULL);
    assert(first.startup_package_owner_id() == 8U);
    assert((first.startup_writable_string_object_ids()
        == std::vector<runtime::simir::StringObjectId> { 0U }));
    assert((first.startup_writable_container_object_ids()
        == std::vector<runtime::simir::ContainerObjectId> { 0U }));
    auto string_write_operation = first.operations().expanded(4U);
    const auto* string_write
        = operation_get_if<runtime::simir::WriteStringObject>(
            &string_write_operation);
    assert(string_write != nullptr && string_write->object == 0U);
    auto container_write_operation = first.operations().expanded(6U);
    const auto* container_write
        = operation_get_if<runtime::simir::WriteContainerObject>(
            &container_write_operation);
    assert(container_write != nullptr && container_write->object == 0U
        && !container_write->nonblocking
        && !container_write->transaction_signal);
    assert(second.startup_phase() == ProcessStartupPhase::ordinary);
    assert(second.startup_order() == 0U);
    assert(independent.startup_phase() == ProcessStartupPhase::ordinary);
    assert(independent.startup_order() == 0U);
}

inline void assert_package_startup_sidecar_rows(const ElaboratedDesign& design)
{
    const auto handles = design.systemverilog_package_class_initializers();
    assert(handles.size() == 1U);
    assert(handles.front().unit.value() == 7U);
    assert(handles.front().declaration.value() == 11U);
    assert(handles.front().signal == 1U);
    assert(handles.front().initializer.value() == 21U);
    assert(handles.front().startup_order == 41U);
    assert((handles.front().writable_signal_ids
        == std::vector<SignalId> { 0U, 1U, 4U }));
    assert((handles.front().writable_string_object_ids
        == std::vector<runtime::simir::StringObjectId> { 0U }));
    assert((handles.front().writable_container_object_ids
        == std::vector<runtime::simir::ContainerObjectId> { 0U }));

    const auto properties
        = design.systemverilog_package_class_static_initializers();
    assert(properties.size() == 1U);
    assert(properties.front().unit.value() == 8U);
    assert(properties.front().class_identity == "work::startup_pkg::owner");
    assert(properties.front().specialization_identity
        == "work::startup_pkg::owner<>");
    assert(properties.front().property_declaration.value() == 12U);
    assert(properties.front().property_name == "item");
    assert(properties.front().canonical_identity
        == "work::startup_pkg::owner::item");
    assert(properties.front().initializer.value() == 22U);
    assert(properties.front().startup_order == 42U);
    assert((properties.front().writable_signal_ids
        == std::vector<SignalId> { 2U, 4U }));
    assert((properties.front().writable_string_object_ids
        == std::vector<runtime::simir::StringObjectId> { 0U }));
    assert((properties.front().writable_container_object_ids
        == std::vector<runtime::simir::ContainerObjectId> { 0U }));
}

inline void assert_startup_metadata_facade_rows(
    const std::vector<Process>& processes)
{
    assert(processes.size() == 3U);
    assert(processes[0U].startup_phase
        == ProcessStartupPhase::package_static_initializer);
    assert(processes[0U].startup_order == 0xfedcba9876543210ULL);
    assert(processes[0U].startup_package_owner_id == 8U);
    assert((processes[0U].startup_writable_string_object_ids
        == std::vector<runtime::simir::StringObjectId> { 0U }));
    assert((processes[0U].startup_writable_container_object_ids
        == std::vector<runtime::simir::ContainerObjectId> { 0U }));
    assert(processes[1U].startup_phase == ProcessStartupPhase::ordinary);
    assert(processes[1U].startup_order == 0U);
    assert(processes[2U].startup_phase == ProcessStartupPhase::ordinary);
    assert(processes[2U].startup_order == 0U);
}

inline bool facade_materialized(runtime::simir::Interpreter& interpreter,
    const runtime::simir::ProcessId process)
{
    return runtime::simir::OwnedDriverDemotionTestAccess::implementation(
        interpreter)
        .processes.public_facade_materialized(process);
}

} // namespace fsim::app::direct_artifact_rows_test
