// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_design_artifact_codec_internal.hpp"
#include "../../src/app/application_hierarchy_path_codec.hpp"
#include "../../src/app/application_runtime_path_codec.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace codec = fsim::app::runtime_path_codec;
namespace codec_detail = fsim::app::codec_detail;
namespace path_codec = fsim::app::hierarchy_path_codec;
using fsim::elaboration::ElaboratedDesignState;
using fsim::runtime::simir::Operation;
using fsim::runtime::simir::OperationList;
using fsim::runtime::simir::Process;
using fsim::runtime::simir::SignalId;

ElaboratedDesignState make_state()
{
    using namespace fsim::runtime::simir;
    ElaboratedDesignState state;
    state.top = "top";
    state.roots.emplace_back("top");

    const auto add_signal = [&](const std::string_view name) {
        const auto id = static_cast<SignalId>(state.signals.size());
        fsim::elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 4U;
        info.type_name = "logic";
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(
            std::string { name },
            fsim::runtime::PackedLogic4 {
                4U, fsim::runtime::Logic4::zero });
        state.signal_names.emplace_back(std::string { name }, id);
        return id;
    };
    const auto input_a = add_signal("top.input_a");
    const auto input_b = add_signal("top.input_b");

    Process representative;
    representative.id = 0U;
    representative.name = "top.template_process";
    representative.register_count = 1U;
    representative.register_value_kinds = { ValueKind::logic4 };
    representative.container_register_count = 1U;
    ContainerType text_register_type;
    text_register_type.element_kind = ContainerElementKind::String;
    text_register_type.element_width = 8U;
    text_register_type.fixed = true;
    ContainerType aggregate_register_type;
    aggregate_register_type.element_kind = ContainerElementKind::Aggregate;
    aggregate_register_type.element_width = 65U;
    aggregate_register_type.index_width = 12U;
    aggregate_register_type.two_state_indices = true;
    aggregate_register_type.index_left = 3;
    aggregate_register_type.index_right = 1;
    aggregate_register_type.maximum_elements = 3U;
    aggregate_register_type.dimensions = { { 3, 1 }, { -2, 0 } };
    aggregate_register_type.element_nominal_type = "packet_t";
    aggregate_register_type.element_types = { text_register_type };
    aggregate_register_type.member_names = { "payload" };
    representative.container_register_types = {
        aggregate_register_type
    };
    representative.static_trigger_regions = { { 0U, 2U, 1U } };
    representative.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    representative.static_sensitivity = {
        { input_a, EdgeKind::any, 1U, 2U }
    };
    representative.driver_regions = {
        { input_a, 1U, 2U, false }
    };
    CoverageControl control;
    control.instance_context = "top.template_control";
    CoverageAccess access;
    access.instance_context = "top.template_access";
    representative.operations = {
        Operation { ReadSignal { 0U, input_a } },
        Operation { WriteUpdateSlice {
            input_a, 0U, 1U,
            SignalUpdateDomain::systemverilog_active } },
        Operation { WaitSensitivity { } },
        Operation { control },
        Operation { access },
        Operation { DebugPoint {
            DebugPointKind::statement,
            SourceLocation { "source.sv", 17U, 4U },
            "top.template_scope" } },
    };

    auto sibling = representative;
    sibling.id = 1U;
    sibling.name = "top.sibling_process";
    sibling.static_sensitivity = {
        { input_b, EdgeKind::any, 2U, 1U }
    };
    sibling.driver_regions = {
        { input_b, 2U, 1U, false }
    };
    auto sibling_read = sibling.operations.expanded(0U);
    operation_get_if<ReadSignal>(&sibling_read)->signal = input_b;
    sibling.operations.replace(0U, std::move(sibling_read));
    auto sibling_write = sibling.operations.expanded(1U);
    operation_get_if<WriteUpdateSlice>(&sibling_write)->signal = input_b;
    sibling.operations.replace(1U, std::move(sibling_write));
    auto sibling_point = sibling.operations.expanded(5U);
    operation_get_if<DebugPoint>(&sibling_point)->scope = "top.sibling_scope";
    sibling.operations.replace(5U, std::move(sibling_point));
    const bool shared = share_process_operations(
        representative, sibling, state.signals);
    assert(shared);

    auto sibling_control = sibling.operations.expanded(3U);
    operation_get_if<CoverageControl>(&sibling_control)->instance_context
        = "top.sibling_control";
    sibling.operations.replace(3U, std::move(sibling_control));
    auto sibling_access = sibling.operations.expanded(4U);
    operation_get_if<CoverageAccess>(&sibling_access)->instance_context
        = "top.sibling_access";
    sibling.operations.replace(4U, std::move(sibling_access));

    auto independent = representative;
    independent.id = 2U;
    independent.name = "top.independent_process";
    independent.register_value_kinds = { ValueKind::logic9 };
    auto independent_container_type = aggregate_register_type;
    independent_container_type.element_width = 64U;
    independent_container_type.member_names = { "other_payload" };
    independent.container_register_types = {
        std::move(independent_container_type)
    };
    independent.static_trigger_regions = { { 1U, 3U, 1U } };
    OperationList::Storage independent_body;
    independent_body.reserve(representative.operations.size());
    for (std::size_t index = 0;
         index < representative.operations.size(); ++index) {
        independent_body.push_back(
            representative.operations.expanded(index));
    }
    independent.operations = std::move(independent_body);

    state.processes.push_back(std::move(representative));
    state.processes.push_back(std::move(sibling));
    state.processes.push_back(std::move(independent));
    return state;
}

fsim::semantic::HierarchyPathTable make_external_paths()
{
    fsim::semantic::HierarchyPathTable::Builder builder;
    for (const auto path : {
             "top", "top.input_a", "top.input_b",
             "top.template_process", "top.sibling_process",
             "top.independent_process", "top.template_scope",
             "top.sibling_scope", "top.template_control",
             "top.sibling_control", "top.template_access",
             "top.sibling_access" }) {
        (void)builder.intern(path);
    }
    const auto paths = std::move(builder).freeze();
    const auto canonical = path_codec::encode_inline_hierarchy_paths(paths);
    assert(canonical);
    return std::move(canonical.payload->paths);
}

void assert_processes(const ElaboratedDesignState& state)
{
    using namespace fsim::runtime::simir;
    assert(state.processes.size() == 3U);
    const auto& representative = state.processes[0];
    const auto& sibling = state.processes[1];
    const auto& independent = state.processes[2];
    assert(representative.operations.shares_body_with(sibling.operations));
    assert(!representative.operations.shares_body_with(independent.operations));
    assert(representative.scheduling_domain
        == ProcessSchedulingDomain::systemverilog);
    assert(sibling.scheduling_domain
        == ProcessSchedulingDomain::systemverilog);
    assert((representative.static_sensitivity
        == std::vector<Sensitivity> { { 0U, EdgeKind::any, 1U, 2U } }));
    assert((sibling.static_sensitivity
        == std::vector<Sensitivity> { { 1U, EdgeKind::any, 2U, 1U } }));
    assert((representative.driver_regions
        == std::vector<Process::DriverRegion> { { 0U, 1U, 2U, false } }));
    assert((sibling.driver_regions
        == std::vector<Process::DriverRegion> { { 1U, 2U, 1U, false } }));
    const auto representative_register_kinds
        = process_layout_detail::ProcessLayoutAccess::view(
            representative.register_value_kinds);
    const auto sibling_register_kinds
        = process_layout_detail::ProcessLayoutAccess::view(
            sibling.register_value_kinds);
    const auto independent_register_kinds
        = process_layout_detail::ProcessLayoutAccess::view(
            independent.register_value_kinds);
    assert((representative_register_kinds.vector()
        == std::vector<ValueKind> { ValueKind::logic4 }));
    assert((sibling_register_kinds.vector()
        == std::vector<ValueKind> { ValueKind::logic4 }));
    assert((independent_register_kinds.vector()
        == std::vector<ValueKind> { ValueKind::logic9 }));
    assert(representative.container_register_count == 1U);
    assert(sibling.container_register_count == 1U);
    assert(independent.container_register_count == 1U);
    assert(process_layout_detail::ProcessLayoutAccess::view(
               representative.container_register_types)
            .vector()
        == process_layout_detail::ProcessLayoutAccess::view(
               sibling.container_register_types)
               .vector());
    assert(process_layout_detail::ProcessLayoutAccess::view(
               representative.container_register_types)
            .vector()
        != process_layout_detail::ProcessLayoutAccess::view(
               independent.container_register_types)
               .vector());
    assert((process_layout_detail::ProcessLayoutAccess::view(
                representative.static_trigger_regions)
                .vector()
        == std::vector<Process::StaticTriggerRegion> { { 0U, 2U, 1U } }));
    assert((process_layout_detail::ProcessLayoutAccess::view(
                independent.static_trigger_regions)
                .vector()
        == std::vector<Process::StaticTriggerRegion> { { 1U, 3U, 1U } }));

    const auto check = [&](const Process& process,
                           const SignalId signal,
                           const std::string_view control_context,
                           const std::string_view access_context,
                           const std::string_view debug_scope) {
        auto read_operation = process.operations.expanded(0U);
        const auto* read = operation_get_if<ReadSignal>(&read_operation);
        assert(read != nullptr && read->signal == signal);
        auto write_operation = process.operations.expanded(1U);
        const auto* write
            = operation_get_if<WriteUpdateSlice>(&write_operation);
        assert(write != nullptr && write->signal == signal);
        assert(write->domain == SignalUpdateDomain::systemverilog_active);
        auto control_operation = process.operations.expanded(3U);
        const auto* control
            = operation_get_if<CoverageControl>(&control_operation);
        assert(control != nullptr
            && control->instance_context == control_context);
        auto access_operation = process.operations.expanded(4U);
        const auto* access
            = operation_get_if<CoverageAccess>(&access_operation);
        assert(access != nullptr
            && access->instance_context == access_context);
        auto debug_operation = process.operations.expanded(5U);
        const auto* point = operation_get_if<DebugPoint>(&debug_operation);
        assert(point != nullptr && point->scope == debug_scope);
    };
    check(representative, 0U, "top.template_control",
        "top.template_access", "top.template_scope");
    check(sibling, 1U, "top.sibling_control",
        "top.sibling_access", "top.sibling_scope");
    check(independent, 0U, "top.template_control",
        "top.template_access", "top.template_scope");
}

void test_runtime_shared_body_roundtrip()
{
    const auto state = make_state();
    assert_processes(state);
    fsim::diagnostic::Engine diagnostics;
    const auto inline_bytes
        = codec::serialize_runtime_path_state(state, nullptr, diagnostics);
    assert(inline_bytes);
    const auto inline_decoded = codec::deserialize_runtime_path_state(
        *inline_bytes, "runtime-shared-inline", nullptr, diagnostics);
    assert(inline_decoded);
    assert_processes(*inline_decoded);
    const auto inline_again = codec::serialize_runtime_path_state(
        *inline_decoded, nullptr, diagnostics);
    assert(inline_again && *inline_again == *inline_bytes);

    const auto external = make_external_paths();
    const auto external_bytes = codec::serialize_runtime_path_state(
        state, &external, diagnostics);
    assert(external_bytes);
    for (const auto path : {
             "top", "top.input_a", "top.input_b",
             "top.template_process", "top.sibling_process",
             "top.independent_process", "top.template_scope",
             "top.sibling_scope", "top.template_control",
             "top.sibling_control", "top.template_access",
             "top.sibling_access" }) {
        assert(external_bytes->find(path) == std::string::npos);
    }
    const auto external_decoded = codec::deserialize_runtime_path_state(
        *external_bytes, "runtime-shared-external", &external, diagnostics);
    assert(external_decoded);
    assert_processes(*external_decoded);
}

void make_layout_backing_unshared(ElaboratedDesignState& state)
{
    using namespace fsim::runtime::simir;
    for (auto& process : state.processes) {
        const auto register_kinds
            = process_layout_detail::ProcessLayoutAccess::view(
                process.register_value_kinds);
        const std::vector<ValueKind> copied_register_kinds {
            register_kinds.begin(), register_kinds.end()
        };
        const auto container_types
            = process_layout_detail::ProcessLayoutAccess::view(
                process.container_register_types);
        const std::vector<ContainerType> copied_container_types {
            container_types.begin(), container_types.end()
        };
        const auto trigger_regions
            = process_layout_detail::ProcessLayoutAccess::view(
                process.static_trigger_regions);
        const std::vector<Process::StaticTriggerRegion> copied_trigger_regions {
            trigger_regions.begin(), trigger_regions.end()
        };
        process.register_value_kinds = copied_register_kinds;
        process.container_register_types = copied_container_types;
        process.static_trigger_regions = copied_trigger_regions;
    }
}

void expose_layout_vectors(ElaboratedDesignState& state)
{
    using namespace fsim::runtime::simir;
    for (const auto& process : state.processes) {
        (void)static_cast<const std::vector<ValueKind>&>(
            process.register_value_kinds).data();
        (void)static_cast<const std::vector<ContainerType>&>(
            process.container_register_types).data();
        (void)static_cast<const std::vector<Process::StaticTriggerRegion>&>(
            process.static_trigger_regions).data();
    }
}

void assert_layout_storage_partition(const ElaboratedDesignState& state)
{
    const auto& representative = state.processes[0];
    const auto& sibling = state.processes[1];
    const auto& independent = state.processes[2];
    assert(representative.register_value_kinds.shares_storage_with(
        sibling.register_value_kinds));
    assert(!representative.register_value_kinds.shares_storage_with(
        independent.register_value_kinds));
    assert(representative.container_register_types.shares_storage_with(
        sibling.container_register_types));
    assert(!representative.container_register_types.shares_storage_with(
        independent.container_register_types));
    assert(representative.static_trigger_regions.shares_storage_with(
        sibling.static_trigger_regions));
    assert(!representative.static_trigger_regions.shares_storage_with(
        independent.static_trigger_regions));
}

void assert_layout_backing_unshared(const ElaboratedDesignState& state)
{
    const auto& representative = state.processes[0];
    const auto& sibling = state.processes[1];
    assert(!representative.register_value_kinds.shares_storage_with(
        sibling.register_value_kinds));
    assert(!representative.container_register_types.shares_storage_with(
        sibling.container_register_types));
    assert(!representative.static_trigger_regions.shares_storage_with(
        sibling.static_trigger_regions));
}

void test_runtime_layout_value_deduplication()
{
    auto shared = make_state();
    auto unshared = make_state();
    auto const_exposed = make_state();
    assert_layout_storage_partition(shared);
    make_layout_backing_unshared(unshared);
    assert_layout_backing_unshared(unshared);
    expose_layout_vectors(const_exposed);

    fsim::diagnostic::Engine diagnostics;
    const auto shared_bytes
        = codec::serialize_runtime_path_state(shared, nullptr, diagnostics);
    const auto unshared_bytes
        = codec::serialize_runtime_path_state(unshared, nullptr, diagnostics);
    const auto exposed_bytes
        = codec::serialize_runtime_path_state(
            const_exposed, nullptr, diagnostics);
    assert(shared_bytes && unshared_bytes && exposed_bytes);
    assert(*shared_bytes == *unshared_bytes);
    assert(*shared_bytes == *exposed_bytes);

    const auto decoded = codec::deserialize_runtime_path_state(
        *shared_bytes, "runtime-layout-sharing", nullptr, diagnostics);
    assert(decoded);
    assert_processes(*decoded);
    assert_layout_storage_partition(*decoded);
}

void append_u64(std::string& bytes, const std::uint64_t value)
{
    const auto offset = bytes.size();
    bytes.resize(offset + 8U);
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        bytes[offset + shift / 8U]
            = static_cast<char>((value >> shift) & 0xffU);
    }
}

std::uint64_t read_u64(const std::string_view bytes, const std::size_t offset)
{
    assert(offset <= bytes.size() && bytes.size() - offset >= 8U);
    std::uint64_t result { };
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        result |= static_cast<std::uint64_t>(
            static_cast<unsigned char>(bytes[offset + shift / 8U]))
            << shift;
    }
    return result;
}

void test_runtime_layout_reference_records()
{
    using namespace fsim::runtime::simir;

    codec_detail::Writer empty_writer;
    empty_writer.set_runtime_process_layout_sharing(true);
    const CopyOnWriteVector<ValueKind> empty_first;
    const CopyOnWriteVector<ValueKind> empty_second;
    empty_writer.write(empty_first);
    empty_writer.write(empty_second);
    assert(empty_writer.failure().empty());
    const auto empty_bytes = std::move(empty_writer).finish();
    codec_detail::Reader empty_reader { empty_bytes };
    empty_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> decoded_empty_first;
    CopyOnWriteVector<ValueKind> decoded_empty_second;
    assert(empty_reader.read(decoded_empty_first));
    assert(empty_reader.read(decoded_empty_second));
    assert(decoded_empty_first.empty());
    assert(decoded_empty_second.empty());
    assert(decoded_empty_first.shares_storage_with(decoded_empty_second));
    assert(empty_reader.remaining() == 0U);

    codec_detail::Writer snapshot_writer;
    snapshot_writer.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> snapshot_source {
        ValueKind::logic4
    };
    snapshot_writer.write(snapshot_source);
    snapshot_source[0U] = ValueKind::logic9;
    snapshot_writer.write(snapshot_source);
    assert(snapshot_writer.failure().empty());
    const auto snapshot_bytes = std::move(snapshot_writer).finish();
    codec_detail::Reader snapshot_reader { snapshot_bytes };
    snapshot_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> before_mutation;
    CopyOnWriteVector<ValueKind> after_mutation;
    assert(snapshot_reader.read(before_mutation));
    assert(snapshot_reader.read(after_mutation));
    assert(before_mutation[0U] == ValueKind::logic4);
    assert(after_mutation[0U] == ValueKind::logic9);
    assert(!before_mutation.shares_storage_with(after_mutation));

    codec_detail::Writer exposed_snapshot_writer;
    exposed_snapshot_writer.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> exposed_snapshot_source {
        ValueKind::logic4
    };
    auto& retained_element = exposed_snapshot_source[0U];
    exposed_snapshot_writer.write(exposed_snapshot_source);
    retained_element = ValueKind::logic9;
    exposed_snapshot_writer.write(exposed_snapshot_source);
    assert(exposed_snapshot_writer.failure().empty());
    const auto exposed_snapshot_bytes
        = std::move(exposed_snapshot_writer).finish();
    codec_detail::Reader exposed_snapshot_reader {
        exposed_snapshot_bytes
    };
    exposed_snapshot_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> before_retained_mutation;
    CopyOnWriteVector<ValueKind> after_retained_mutation;
    assert(exposed_snapshot_reader.read(before_retained_mutation));
    assert(exposed_snapshot_reader.read(after_retained_mutation));
    assert(before_retained_mutation[0U] == ValueKind::logic4);
    assert(after_retained_mutation[0U] == ValueKind::logic9);
    assert(!before_retained_mutation.shares_storage_with(
        after_retained_mutation));

    std::string unknown_reference;
    append_u64(unknown_reference, 0U);
    unknown_reference.push_back('\0');
    codec_detail::Reader unknown_reader { unknown_reference };
    unknown_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> unknown_value;
    assert(!unknown_reader.read(unknown_value));
    assert(unknown_reader.failure().find("unknown process-layout")
        != std::string::npos);

    std::string duplicate_definition;
    for (unsigned occurrence = 0U; occurrence < 2U; ++occurrence) {
        append_u64(duplicate_definition, 0U);
        duplicate_definition.push_back('\1');
        append_u64(duplicate_definition, 0U);
    }
    codec_detail::Reader duplicate_reader { duplicate_definition };
    duplicate_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> first_definition;
    CopyOnWriteVector<ValueKind> duplicate_value;
    assert(duplicate_reader.read(first_definition));
    assert(!duplicate_reader.read(duplicate_value));
    assert(duplicate_reader.failure().find("duplicated or out of order")
        != std::string::npos);

    std::string out_of_order_definition;
    append_u64(out_of_order_definition, 1U);
    out_of_order_definition.push_back('\1');
    append_u64(out_of_order_definition, 0U);
    codec_detail::Reader order_reader { out_of_order_definition };
    order_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> out_of_order_value;
    assert(!order_reader.read(out_of_order_value));
    assert(order_reader.failure().find("duplicated or out of order")
        != std::string::npos);

    std::string malformed_size;
    append_u64(malformed_size, 0U);
    malformed_size.push_back('\1');
    append_u64(malformed_size, 2U);
    malformed_size.push_back('\0');
    codec_detail::Reader size_reader { malformed_size };
    size_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> malformed_size_value;
    assert(!size_reader.read(malformed_size_value));
    assert(size_reader.failure().find("exceeds the payload")
        != std::string::npos);

    codec_detail::Writer typed_writer;
    typed_writer.set_runtime_process_layout_sharing(true);
    const CopyOnWriteVector<ValueKind> kind_definition {
        ValueKind::logic4
    };
    typed_writer.write(kind_definition);
    auto typed_bytes = std::move(typed_writer).finish();
    append_u64(typed_bytes, 0U);
    typed_bytes.push_back('\0');
    codec_detail::Reader typed_reader { typed_bytes };
    typed_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> decoded_kind;
    CopyOnWriteVector<ContainerType> wrong_typed_reference;
    assert(typed_reader.read(decoded_kind));
    assert(!typed_reader.read(wrong_typed_reference));
    assert(typed_reader.failure().find("unknown process-layout")
        != std::string::npos);

    codec_detail::Writer budget_writer;
    budget_writer.set_runtime_process_layout_sharing(true);
    budget_writer.write(kind_definition);
    const auto budget_bytes = std::move(budget_writer).finish();
    codec_detail::DecodeBudget budget { 0U };
    codec_detail::Reader budget_reader { budget_bytes, &budget };
    budget_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> budget_value;
    assert(!budget_reader.read(budget_value));
    assert(budget_reader.failure().find("allocation budget")
        != std::string::npos);

    codec_detail::Reader exposed_target_reader { budget_bytes };
    exposed_target_reader.set_runtime_process_layout_sharing(true);
    CopyOnWriteVector<ValueKind> exposed_target { ValueKind::logic9 };
    auto& retained_target_value = exposed_target[0U];
    assert(!exposed_target_reader.read(exposed_target));
    assert(exposed_target_reader.failure().find("already exposed")
        != std::string::npos);
    assert(retained_target_value == ValueKind::logic9);
}

void test_runtime_layout_process_shape_validation()
{
    using namespace fsim::runtime::simir;
    Process invalid_process;
    invalid_process.register_count = 2U;
    invalid_process.register_value_kinds = { ValueKind::logic4 };
    invalid_process.container_register_count = 1U;

    codec_detail::Writer ordinary_writer;
    ordinary_writer.write(invalid_process);
    assert(ordinary_writer.failure().empty());
    const auto ordinary_bytes = std::move(ordinary_writer).finish();
    codec_detail::Reader ordinary_reader { ordinary_bytes };
    Process ordinary_decoded;
    assert(ordinary_reader.read(ordinary_decoded));
    assert(ordinary_reader.remaining() == 0U);

    codec_detail::Writer writer;
    writer.set_runtime_process_layout_sharing(true);
    writer.write(invalid_process);
    assert(writer.failure().empty());
    const auto bytes = std::move(writer).finish();

    codec_detail::Reader reader { bytes };
    reader.set_runtime_process_layout_sharing(true);
    Process decoded;
    assert(!reader.read(decoded));
    assert(reader.failure().find("invalid register shape")
        != std::string::npos);

    Process invalid_container_shape;
    invalid_container_shape.register_count = 1U;
    invalid_container_shape.register_value_kinds = { ValueKind::logic4 };
    invalid_container_shape.container_register_count = 1U;
    codec_detail::Writer container_writer;
    container_writer.set_runtime_process_layout_sharing(true);
    container_writer.write(invalid_container_shape);
    assert(container_writer.failure().empty());
    const auto container_bytes = std::move(container_writer).finish();
    codec_detail::Reader container_reader { container_bytes };
    container_reader.set_runtime_process_layout_sharing(true);
    Process decoded_container_shape;
    assert(!container_reader.read(decoded_container_shape));
    assert(container_reader.failure().find("invalid register shape")
        != std::string::npos);

    std::vector<Process> repeated_processes(2U);
    repeated_processes[0U].register_count = 1U;
    repeated_processes[0U].register_value_kinds = { ValueKind::logic4 };
    repeated_processes[1U] = repeated_processes[0U];
    codec_detail::Writer repeated_writer;
    repeated_writer.set_runtime_process_layout_sharing(true);
    repeated_writer.write(repeated_processes);
    assert(repeated_writer.failure().empty());
    const auto repeated_bytes = std::move(repeated_writer).finish();
    codec_detail::DecodeBudget repeated_budget {
        2U * sizeof(Process) - 1U
    };
    codec_detail::Reader repeated_reader {
        repeated_bytes, &repeated_budget
    };
    repeated_reader.set_runtime_process_layout_sharing(true);
    std::vector<Process> decoded_repeated_processes;
    assert(!repeated_reader.read(decoded_repeated_processes));
    assert(repeated_reader.failure().find("allocation budget")
        != std::string::npos);
}

std::string encode_operations(const OperationList& operations)
{
    codec_detail::Writer writer;
    writer.set_runtime_operation_body_sharing(true);
    writer.write(operations);
    assert(writer.failure().empty());
    return std::move(writer).finish();
}

std::string with_overrides(
    const std::string& body, const std::vector<std::uint64_t>& indices)
{
    constexpr std::size_t kBodyOperationsOffset = 17U;
    const auto operation_count = read_u64(body, 9U);
    const auto end = body.size() - 8U;
    assert(end > kBodyOperationsOffset);
    assert(operation_count != 0U);
    const auto encoded_operations_size = end - kBodyOperationsOffset;
    assert(encoded_operations_size % operation_count == 0U);
    const auto one_operation_size = static_cast<std::size_t>(
        encoded_operations_size / operation_count);
    const std::string operation_bytes {
        body.substr(kBodyOperationsOffset, one_operation_size) };
    std::string result { body.substr(0U, end) };
    append_u64(result, indices.size());
    for (const auto index : indices) {
        append_u64(result, index);
        result.append(operation_bytes);
    }
    return result;
}

void test_untrusted_operation_body_records()
{
    using namespace fsim::runtime::simir;
    std::string unknown_reference;
    append_u64(unknown_reference, 0U);
    unknown_reference.push_back('\0');
    append_u64(unknown_reference, 0U);
    codec_detail::Reader unknown_reader { unknown_reference };
    unknown_reader.set_runtime_operation_body_sharing(true);
    OperationList unknown_result;
    assert(!unknown_reader.read(unknown_result));
    assert(unknown_reader.failure().find("unknown operation body")
        != std::string::npos);

    const OperationList one_operation {
        Operation { WaitSensitivity { } }
    };
    const auto one_body = encode_operations(one_operation);
    const auto out_of_range = with_overrides(one_body, { 1U });
    codec_detail::Reader range_reader { out_of_range };
    range_reader.set_runtime_operation_body_sharing(true);
    OperationList range_result;
    assert(!range_reader.read(range_result));
    assert(range_reader.failure().find("not canonical") != std::string::npos);

    const OperationList two_operations {
        Operation { WaitSensitivity { } },
        Operation { WaitSensitivity { } },
    };
    const auto two_body = encode_operations(two_operations);
    const auto duplicate = with_overrides(two_body, { 0U, 0U });
    codec_detail::Reader duplicate_reader { duplicate };
    duplicate_reader.set_runtime_operation_body_sharing(true);
    OperationList duplicate_result;
    assert(!duplicate_reader.read(duplicate_result));
    assert(duplicate_reader.failure().find("not canonical")
        != std::string::npos);

    const auto budgeted = with_overrides(one_body, { 0U });
    codec_detail::DecodeBudget budget {
        sizeof(OperationList) + sizeof(Operation)
            + sizeof(std::pair<std::uint64_t, Operation>) - 1U
    };
    codec_detail::Reader budget_reader { budgeted, &budget };
    budget_reader.set_runtime_operation_body_sharing(true);
    OperationList budget_result;
    assert(!budget_reader.read(budget_result));
    assert(budget_reader.failure().find("aggregate decode allocation budget")
        != std::string::npos);
}

void test_empty_body_partition()
{
    OperationList first;
    OperationList second = first;
    assert(first.shares_body_with(second));

    codec_detail::Writer writer;
    writer.set_runtime_operation_body_sharing(true);
    writer.write(first);
    writer.write(second);
    assert(writer.failure().empty());
    const auto bytes = std::move(writer).finish();
    codec_detail::Reader reader { bytes };
    reader.set_runtime_operation_body_sharing(true);
    OperationList decoded_first;
    OperationList decoded_second;
    assert(reader.read(decoded_first));
    assert(reader.read(decoded_second));
    assert(decoded_first.shares_body_with(decoded_second));
    codec_detail::Writer second_writer;
    second_writer.set_runtime_operation_body_sharing(true);
    second_writer.write(decoded_first);
    second_writer.write(decoded_second);
    assert(second_writer.failure().empty());
    assert(std::move(second_writer).finish() == bytes);
}

void test_default_codec_uses_expanded_operation_content()
{
    const OperationList shared_template {
        Operation { fsim::runtime::simir::WaitSensitivity { } },
        Operation { fsim::runtime::simir::WaitSensitivity { } },
    };
    const auto shared_instance = shared_template;
    const OperationList independent_a {
        Operation { fsim::runtime::simir::WaitSensitivity { } },
        Operation { fsim::runtime::simir::WaitSensitivity { } },
    };
    const OperationList independent_b {
        Operation { fsim::runtime::simir::WaitSensitivity { } },
        Operation { fsim::runtime::simir::WaitSensitivity { } },
    };
    assert(shared_template.shares_body_with(shared_instance));
    assert(!independent_a.shares_body_with(independent_b));

    codec_detail::Writer shared_writer;
    shared_writer.write(shared_template);
    shared_writer.write(shared_instance);
    codec_detail::Writer independent_writer;
    independent_writer.write(independent_a);
    independent_writer.write(independent_b);
    assert(shared_writer.failure().empty());
    assert(independent_writer.failure().empty());
    assert(std::move(shared_writer).finish()
        == std::move(independent_writer).finish());

    const OperationList signal_template {
        Operation { fsim::runtime::simir::ReadSignal { 0U, 0U } }
    };
    auto specialized = signal_template;
    auto specialized_read = specialized.expanded(0U);
    fsim::runtime::simir::operation_get_if<
        fsim::runtime::simir::ReadSignal>(&specialized_read)->signal = 1U;
    specialized.replace(0U, std::move(specialized_read));
    const OperationList independent_specialization {
        Operation { fsim::runtime::simir::ReadSignal { 0U, 1U } }
    };
    codec_detail::Writer specialized_writer;
    specialized_writer.write(specialized);
    codec_detail::Writer independent_specialization_writer;
    independent_specialization_writer.write(independent_specialization);
    assert(specialized_writer.failure().empty());
    assert(independent_specialization_writer.failure().empty());
    assert(std::move(specialized_writer).finish()
        == std::move(independent_specialization_writer).finish());

    auto state = make_state();
    // This default-codec witness checks expanded operation interning. The
    // shared-layout witness above deliberately gives process 2 other metadata.
    state.processes[2U].register_value_kinds
        = state.processes[0U].register_value_kinds;
    state.processes[2U].container_register_types
        = state.processes[0U].container_register_types;
    state.processes[2U].static_trigger_regions
        = state.processes[0U].static_trigger_regions;
    codec_detail::Writer state_writer;
    state_writer.write(state);
    assert(state_writer.failure().empty());
    const auto state_bytes = std::move(state_writer).finish();
    codec_detail::Reader state_reader { state_bytes };
    ElaboratedDesignState decoded;
    assert(state_reader.read(decoded));
    assert(state_reader.remaining() == 0U);
    assert(decoded.processes[0].operations.shares_body_with(
        decoded.processes[2].operations));
    assert(!decoded.processes[0].operations.shares_body_with(
        decoded.processes[1].operations));
}

void test_operation_override_copy_isolation_and_artifact_bytes()
{
    using fsim::runtime::simir::Concatenate;
    using fsim::runtime::simir::RegisterId;
    using fsim::runtime::simir::SignalUpdateDomain;
    using fsim::runtime::simir::WaitSensitivity;
    using fsim::runtime::simir::WriteUpdate;
    using fsim::runtime::simir::operation_get_if;

    const OperationList body {
        Operation { WaitSensitivity { } },
        Operation { WaitSensitivity { } },
    };
    OperationList original = body;
    original.replace(0U, Operation { Concatenate {
        7U, { 1U, 2U, 3U, 4U }, 16U
    } });
    original.replace(1U, Operation { WriteUpdate {
        5U, 6U, SignalUpdateDomain::systemverilog_active
    } });

    const auto row_copy = original;
    auto runtime_copy = row_copy;
    auto snapshot_copy = runtime_copy;
    const auto original_revision = original.access_revision();
    const Operation* const shared_write_update
        = &std::as_const(original)[1U];
    assert(shared_write_update == &std::as_const(row_copy)[1U]);
    assert(shared_write_update == &std::as_const(runtime_copy)[1U]);
    assert(shared_write_update == &std::as_const(snapshot_copy)[1U]);

    const auto has_concatenate = [](const OperationList& operations,
                                     const std::vector<RegisterId>& operands,
                                     const std::uint32_t width) {
        const auto* const operation
            = operation_get_if<Concatenate>(&operations[0U]);
        return operation != nullptr && operation->operands == operands
            && operation->width == width;
    };
    const auto has_write_update = [](
        const OperationList& operations,
        const fsim::runtime::simir::SignalId signal,
        const RegisterId source,
        const SignalUpdateDomain domain) {
        const auto* const operation
            = operation_get_if<WriteUpdate>(&operations[1U]);
        return operation != nullptr && operation->signal == signal
            && operation->source == source && operation->domain == domain;
    };
    const std::vector<RegisterId> original_operands {
        1U, 2U, 3U, 4U
    };
    assert(has_concatenate(original, original_operands, 16U));
    assert(has_concatenate(row_copy, original_operands, 16U));
    assert(has_concatenate(runtime_copy, original_operands, 16U));
    assert(has_concatenate(snapshot_copy, original_operands, 16U));
    assert(has_write_update(
        original, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(has_write_update(
        row_copy, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(has_write_update(
        runtime_copy, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(has_write_update(
        snapshot_copy, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(original.instance_operation_overrides().size() == 2U);
    assert(original.shares_body_with(row_copy));

    runtime_copy.replace(0U, Operation { Concatenate {
        7U, { 8U, 9U, 10U }, 12U
    } });
    assert(has_concatenate(runtime_copy,
        std::vector<RegisterId> { 8U, 9U, 10U }, 12U));
    assert(has_concatenate(original, original_operands, 16U));
    assert(has_concatenate(row_copy, original_operands, 16U));
    assert(has_concatenate(snapshot_copy, original_operands, 16U));
    assert(has_write_update(
        original, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(has_write_update(
        row_copy, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(has_write_update(
        snapshot_copy, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(&std::as_const(runtime_copy)[1U] != shared_write_update);
    assert(original.access_revision() == original_revision);
    assert(snapshot_copy.access_revision() == original_revision);
    assert(runtime_copy.access_revision() != original_revision);

    snapshot_copy[1U] = Operation { WriteUpdate {
        9U, 10U, SignalUpdateDomain::systemverilog_nba
    } };
    assert(has_write_update(
        snapshot_copy, 9U, 10U, SignalUpdateDomain::systemverilog_nba));
    assert(&std::as_const(snapshot_copy)[1U] != shared_write_update);
    assert(&std::as_const(original)[1U] == shared_write_update);
    assert(&std::as_const(row_copy)[1U] == shared_write_update);
    assert(has_write_update(
        original, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(has_write_update(
        row_copy, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(has_write_update(
        runtime_copy, 5U, 6U, SignalUpdateDomain::systemverilog_active));
    assert(has_concatenate(snapshot_copy,
        std::vector<RegisterId> { 1U, 2U, 3U, 4U }, 16U));
    assert(has_concatenate(original, original_operands, 16U));
    assert(has_concatenate(runtime_copy,
        std::vector<RegisterId> { 8U, 9U, 10U }, 12U));

    OperationList independently_built {
        Operation { WaitSensitivity { } },
        Operation { WaitSensitivity { } },
    };
    assert(!original.shares_body_with(independently_built));
    independently_built.replace(0U, Operation { Concatenate {
        7U, original_operands, 16U
    } });
    independently_built.replace(1U, Operation { WriteUpdate {
        5U, 6U, SignalUpdateDomain::systemverilog_active
    } });
    const auto encode = [](const OperationList& operations) {
        codec_detail::Writer writer;
        writer.write(operations);
        assert(writer.failure().empty());
        return std::move(writer).finish();
    };
    assert(encode(original) == encode(independently_built));
    assert(encode(original) != encode(runtime_copy));
}

} // namespace

int main()
{
    test_runtime_shared_body_roundtrip();
    test_runtime_layout_value_deduplication();
    test_runtime_layout_reference_records();
    test_runtime_layout_process_shape_validation();
    test_untrusted_operation_body_records();
    test_empty_body_partition();
    test_default_codec_uses_expanded_operation_content();
    test_operation_override_copy_isolation_and_artifact_bytes();
}
