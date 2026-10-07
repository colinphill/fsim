// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_hierarchy_path_codec.hpp"
#include "../../src/app/application_runtime_path_codec.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace codec = fsim::app::runtime_path_codec;
namespace path_codec = fsim::app::hierarchy_path_codec;
using fsim::elaboration::ElaboratedDesignState;
using fsim::semantic::HierarchyPathTable;

constexpr std::string_view kRoot = "top";
constexpr std::string_view kSignalPath = "top.signal";

ElaboratedDesignState make_state()
{
    ElaboratedDesignState state;
    state.top = kRoot;
    state.roots.emplace_back(kRoot);

    fsim::elaboration::SignalInfo signal_info;
    signal_info.name = kSignalPath;
    signal_info.type_name = "logic";
    fsim::elaboration::VhdlModeViewBinding binding;
    binding.formal = "record_formal";
    binding.view = "record_view";
    fsim::elaboration::VhdlModeViewElementBinding element;
    element.formal_path = "top.child.formal";
    element.actual_path = "top.child.actual";
    binding.elements.push_back(std::move(element));
    signal_info.vhdl_mode_view_bindings.push_back(std::move(binding));
    state.signal_info.push_back(std::move(signal_info));
    state.signals.emplace_back(std::string { kSignalPath },
        fsim::runtime::PackedLogic4 { });

    fsim::elaboration::BoundaryConversionInfo conversion;
    conversion.path = "top.boundary";
    state.boundary_conversions.push_back(std::move(conversion));

    fsim::elaboration::StringObjectInfo string_info;
    string_info.name = "top.string";
    state.string_object_info.push_back(std::move(string_info));
    state.string_objects.push_back(
        { "top.string", "initial string" });

    fsim::elaboration::ContainerObjectInfo container_info;
    container_info.name = "top.array";
    state.container_object_info.push_back(std::move(container_info));
    state.container_objects.push_back({ "top.array", { }, std::nullopt });
    fsim::runtime::simir::ContainerType array_type;
    array_type.element_width = 4U;
    array_type.fixed = true;
    array_type.index_left = 1;
    array_type.index_right = 0;
    array_type.dimensions = { { 1, 0 } };
    state.container_object_info.front().type = array_type;
    state.container_objects.front().initial_value = {
        array_type,
        { fsim::runtime::PackedLogic4::from_aval_bval(4U, 0xaU, 0U),
            fsim::runtime::PackedLogic4::from_aval_bval(4U, 0x5U, 0U) },
        { }
    };
    const auto add_element_signal = [&](const std::string_view path,
                                        const std::uint64_t initial) {
        const auto id = static_cast<fsim::runtime::simir::SignalId>(
            state.signals.size());
        fsim::elaboration::SignalInfo info;
        info.id = id;
        info.name = path;
        info.width = 4U;
        info.type_name = "logic[3:0]";
        info.source_domain = fsim::frontend::ValueDomain::Logic4;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(std::string { path },
            fsim::runtime::PackedLogic4::from_aval_bval(
                4U, initial, 0U),
            fsim::runtime::simir::ResolutionKind::sv_wire);
        state.signal_names.emplace_back(std::string { path }, id);
        return id;
    };
    const auto array_first
        = add_element_signal("top.array[1]", 0xaU);
    const auto array_second
        = add_element_signal("top.array[0]", 0x5U);
    state.container_element_signal_aliases.push_back(
        { 0U, 0U, array_first, true, true });
    state.container_element_signal_aliases.push_back(
        { 0U, 1U, array_second, true, true });
    const auto array_aggregate
        = static_cast<fsim::runtime::simir::SignalId>(state.signals.size());
    fsim::elaboration::SignalInfo aggregate_info;
    aggregate_info.id = array_aggregate;
    aggregate_info.name = "top.array";
    aggregate_info.width = 8U;
    aggregate_info.type_name = "logic [3:0] [1:0]";
    aggregate_info.source_domain = fsim::frontend::ValueDomain::Logic4;
    state.signal_info.push_back(std::move(aggregate_info));
    state.signals.emplace_back("top.array",
        fsim::runtime::PackedLogic4::from_msb_string("10100101"),
        fsim::runtime::simir::ResolutionKind::sv_wire);
    state.container_aggregate_signal_aliases.push_back(
        { 0U, array_aggregate, true, false });
    state.signal_names.emplace_back("top.array", array_aggregate);

    fsim::elaboration::VhdlProtectedObjectInfo protected_info;
    protected_info.name = "top.protected";
    protected_info.type_name = "ProtectedType";
    protected_info.nominal_type = "work.ProtectedType";
    protected_info.members.push_back({ "member_name", false, 0U, 0U, 0U,
        { } });
    state.vhdl_protected_object_info.push_back(
        std::move(protected_info));

    fsim::runtime::simir::Process process;
    process.name = "top.process";
    fsim::runtime::simir::CoverageControl control;
    control.instance_context = "top.control_context";
    process.operations.emplace_back(control);
    fsim::runtime::simir::CoverageAccess access;
    access.selector = 0U;
    access.instance_context = "top.access_context";
    process.operations.emplace_back(access);
    fsim::runtime::simir::CoverageAccess file_access;
    file_access.instance_context.clear();
    process.operations.emplace_back(file_access);
    state.processes.push_back(std::move(process));

    fsim::elaboration::SpecializationInfo specialization;
    specialization.instance = "top.specialization";
    specialization.unit = "TopUnit";
    specialization.source = "source.sv";
    state.specializations.push_back(std::move(specialization));

    fsim::elaboration::VerilogSpecifyPathInfo specify;
    specify.instance = "top.specify";
    specify.identity = "sdf:iopath:top.specify:0";
    state.verilog_specify_paths.push_back(std::move(specify));

    fsim::elaboration::SystemCInstanceInfo instance;
    instance.instance = "top.systemc";
    instance.target = "systemc:library.factory";
    state.systemc_instances.push_back(std::move(instance));

    fsim::elaboration::SystemCNamedObjectInfo object;
    object.name = "top.systemc";
    object.parent.clear();
    object.type_name = "NativeModule";
    state.systemc_objects.push_back(std::move(object));

    state.signal_names.emplace_back(std::string { kSignalPath }, 0U);
    state.string_names.emplace_back("top.string", 0U);
    state.container_names.emplace_back("top.array", 0U);

    fsim::elaboration::CodeCoverageInventory coverage;
    fsim::elaboration::CoverageInstanceInventory coverage_instance;
    coverage_instance.instance = "top.coverage";
    coverage.instances.push_back(std::move(coverage_instance));
    state.code_coverage_inventory = std::move(coverage);
    return state;
}

ElaboratedDesignState make_alias_validation_state()
{
    ElaboratedDesignState state;
    state.top = kRoot;
    state.roots.emplace_back(kRoot);

    fsim::runtime::simir::ContainerType array_type;
    array_type.element_width = 4U;
    array_type.fixed = true;
    array_type.index_left = 1;
    array_type.index_right = 0;
    array_type.dimensions = { { 1, 0 } };

    fsim::elaboration::ContainerObjectInfo array_info;
    array_info.name = "top.array";
    array_info.type = array_type;
    state.container_object_info.push_back(std::move(array_info));
    state.container_objects.push_back({ "top.array",
        { array_type,
            { fsim::runtime::PackedLogic4::from_aval_bval(4U, 0xaU, 0U),
                fsim::runtime::PackedLogic4::from_aval_bval(4U, 0x5U, 0U) },
            { } },
        std::nullopt });
    state.container_names.emplace_back("top.array", 0U);

    const auto add_signal = [&](const std::string_view name,
                                const std::uint32_t width,
                                fsim::runtime::PackedLogic4 initial_value) {
        const auto id = static_cast<fsim::runtime::simir::SignalId>(
            state.signals.size());
        fsim::elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = width;
        info.type_name = "logic";
        info.source_domain = fsim::frontend::ValueDomain::Logic4;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(std::string { name },
            std::move(initial_value),
            fsim::runtime::simir::ResolutionKind::sv_wire);
        state.signal_names.emplace_back(std::string { name }, id);
        return id;
    };

    const auto first = add_signal("top.array[1]", 4U,
        fsim::runtime::PackedLogic4::from_aval_bval(4U, 0xaU, 0U));
    const auto second = add_signal("top.array[0]", 4U,
        fsim::runtime::PackedLogic4::from_aval_bval(4U, 0x5U, 0U));
    const auto aggregate = add_signal("top.array", 8U,
        fsim::runtime::PackedLogic4::from_msb_string("10100101"));
    state.container_element_signal_aliases = {
        { 0U, 0U, first, true, true },
        { 0U, 1U, second, true, true }
    };
    state.container_aggregate_signal_aliases.push_back(
        { 0U, aggregate, true, false });
    fsim::runtime::simir::Process owner;
    owner.id = 0U;
    owner.name = "top.array_leaf_owner";
    owner.register_count = 1U;
    owner.driver_regions = { { first, 0U, 0U, true } };
    owner.operations = {
        fsim::runtime::simir::LoadConstant {
            0U, fsim::runtime::PackedLogic4 { 4U,
                fsim::runtime::Logic4::zero } },
        fsim::runtime::simir::WriteUpdate { first, 0U,
            fsim::runtime::simir::SignalUpdateDomain::systemverilog_active },
        fsim::runtime::simir::Halt { }
    };
    state.processes.push_back(std::move(owner));
    return state;
}

HierarchyPathTable make_external_table(const bool canonicalize = true)
{
    HierarchyPathTable::Builder builder;
    for (const auto path : {
             "zz.design_only", "top.signal", "top", "top.array",
             "top.array_leaf_owner", "top.array[1]", "top.array[0]",
             "top.string", "top.child.formal", "top.child.actual",
             "top.boundary", "top.protected", "top.process",
             "top.process.sibling", "top.sibling_control_context",
             "top.sibling_access_context", "top.process_control_context",
             "top.process_access_context",
             "top.control_context", "top.access_context",
             "top.specialization", "top.specify", "top.systemc",
             "top.coverage" }) {
        (void)builder.intern(path);
    }
    auto paths = std::move(builder).freeze();
    if (!canonicalize) {
        return paths;
    }
    auto canonical = path_codec::encode_inline_hierarchy_paths(paths);
    assert(canonical);
    return std::move(canonical.payload->paths);
}

std::uint64_t read_u64(
    const std::string_view bytes, const std::size_t offset)
{
    assert(offset <= bytes.size());
    assert(bytes.size() - offset >= 8U);
    std::uint64_t value { };
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        value |= static_cast<std::uint64_t>(
            static_cast<unsigned char>(bytes[offset + shift / 8U]))
            << shift;
    }
    return value;
}

void write_u64(
    std::string& bytes, const std::size_t offset, const std::uint64_t value)
{
    assert(offset <= bytes.size());
    assert(bytes.size() - offset >= 8U);
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        bytes[offset + shift / 8U]
            = static_cast<char>((value >> shift) & 0xffU);
    }
}

void write_u32(
    std::string& bytes, const std::size_t offset, const std::uint32_t value)
{
    assert(offset <= bytes.size());
    assert(bytes.size() - offset >= 4U);
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        bytes[offset + shift / 8U]
            = static_cast<char>((value >> shift) & 0xffU);
    }
}

std::string replace_inline_table(
    const std::string_view payload,
    const std::string_view replacement_table)
{
    // Magic, schema and path mode precede the binding; as a string-table
    // entry it starts with reference 0, then its length.
    constexpr std::size_t kBindingLengthOffset = 21U;
    constexpr std::size_t kBindingBytesOffset = 29U;
    const auto old_size = static_cast<std::size_t>(
        read_u64(payload, kBindingLengthOffset));
    assert(kBindingBytesOffset + old_size <= payload.size());
    std::string result { payload.substr(0U, kBindingLengthOffset) };
    const auto length_offset = result.size();
    result.resize(result.size() + 8U);
    write_u64(result, length_offset, replacement_table.size());
    result.append(replacement_table);
    result.append(payload.substr(kBindingBytesOffset + old_size));
    return result;
}

void assert_decoded_paths(const ElaboratedDesignState& state)
{
    assert(state.top == "top");
    assert(state.roots == std::vector<std::string> { "top" });
    assert(state.signal_info.front().name == "top.signal");
    assert(state.signal_info.front().type_name == "logic");
    const auto& endpoint
        = state.signal_info.front().vhdl_mode_view_bindings.front()
              .elements.front();
    assert(endpoint.formal_path == "top.child.formal");
    assert(endpoint.actual_path == "top.child.actual");
    assert(state.boundary_conversions.front().path == "top.boundary");
    assert(state.signals.front().name == "top.signal");
    assert(state.string_object_info.front().name == "top.string");
    assert(state.string_objects.front().name == "top.string");
    assert(state.container_object_info.front().name == "top.array");
    assert(state.container_objects.front().name == "top.array");
    assert(state.signal_info[1].name == "top.array[1]");
    assert(state.signal_info[2].name == "top.array[0]");
    assert(state.signal_info[3].name == "top.array");
    assert(state.container_element_signal_aliases.size() == 2U);
    assert((state.container_element_signal_aliases[0]
        == fsim::runtime::simir::ContainerElementSignalAlias {
            0U, 0U, 1U, true, true }));
    assert((state.container_element_signal_aliases[1]
        == fsim::runtime::simir::ContainerElementSignalAlias {
            0U, 1U, 2U, true, true }));
    assert((state.container_aggregate_signal_aliases
        == std::vector<fsim::runtime::simir::ContainerAggregateSignalAlias> {
            { 0U, 3U, true, false } }));
    assert(state.vhdl_protected_object_info.front().name == "top.protected");
    assert(state.vhdl_protected_object_info.front().members.front().name
        == "member_name");
    assert(state.processes.front().name == "top.process");

    auto control = state.processes.front().operations.expanded(0U);
    const auto* control_operation
        = fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::CoverageControl>(&control);
    assert(control_operation != nullptr);
    assert(control_operation->instance_context == "top.control_context");
    auto access = state.processes.front().operations.expanded(1U);
    const auto* access_operation
        = fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::CoverageAccess>(&access);
    assert(access_operation != nullptr);
    assert(access_operation->instance_context == "top.access_context");
    auto file_access = state.processes.front().operations.expanded(2U);
    const auto* file_access_operation
        = fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::CoverageAccess>(&file_access);
    assert(file_access_operation != nullptr);
    assert(file_access_operation->instance_context.empty());

    assert(state.specializations.front().instance == "top.specialization");
    assert(state.specializations.front().unit == "TopUnit");
    assert(state.specializations.front().source == "source.sv");
    assert(state.verilog_specify_paths.front().instance == "top.specify");
    assert(state.systemc_instances.front().instance == "top.systemc");
    assert(state.systemc_instances.front().target
        == "systemc:library.factory");
    assert(state.systemc_objects.front().name == "top.systemc");
    assert(state.systemc_objects.front().parent.empty());
    assert(state.systemc_objects.front().type_name == "NativeModule");
    const auto root_signal_name = std::ranges::find_if(
        state.signal_names, [](const auto& entry) {
            return entry.first == "top.signal";
        });
    assert(root_signal_name != state.signal_names.end()
        && root_signal_name->second == 0U);
    assert(state.string_names.front().first == "top.string");
    assert(state.container_names.front().first == "top.array");
    assert(state.code_coverage_inventory->instances.front().instance
        == "top.coverage");
}

ElaboratedDesignState make_shared_context_override_state()
{
    auto state = make_state();
    auto first = state.processes.front();
    auto sibling = first;
    sibling.id = 1U;
    sibling.name = "top.process.sibling";

    const auto set_context = [](auto& process,
                                const std::size_t index,
                                std::string context) {
        auto operation = process.operations.expanded(index);
        if (auto* control
            = fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::CoverageControl>(&operation)) {
            control->instance_context = std::move(context);
        } else {
            auto* access
                = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::CoverageAccess>(&operation);
            assert(access != nullptr);
            access->instance_context = std::move(context);
        }
        process.operations.replace(index, std::move(operation));
    };
    set_context(first, 0U, "top.process_control_context");
    set_context(first, 1U, "top.process_access_context");
    set_context(sibling, 0U, "top.sibling_control_context");
    set_context(sibling, 1U, "top.sibling_access_context");
    assert(first.operations.shares_body_with(sibling.operations));
    state.processes.front() = std::move(first);
    state.processes.push_back(std::move(sibling));
    return state;
}

void assert_instance_contexts(const ElaboratedDesignState& state)
{
    assert(state.processes.size() == 2U);
    assert(state.processes[0].name == "top.process");
    assert(state.processes[1].name == "top.process.sibling");
    const auto context = [](const auto& process, const std::size_t index) {
        auto operation = process.operations.expanded(index);
        if (const auto* control
            = fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::CoverageControl>(&operation)) {
            return control->instance_context;
        }
        const auto* access
            = fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::CoverageAccess>(&operation);
        assert(access != nullptr);
        return access->instance_context;
    };
    assert(context(state.processes[0], 0U)
        == "top.process_control_context");
    assert(context(state.processes[0], 1U)
        == "top.process_access_context");
    assert(context(state.processes[1], 0U)
        == "top.sibling_control_context");
    assert(context(state.processes[1], 1U)
        == "top.sibling_access_context");
}

void test_shared_operation_context_path_projection()
{
    const auto state = make_shared_context_override_state();
    assert(state.processes[0].operations.shares_body_with(
        state.processes[1].operations));
    fsim::diagnostic::Engine diagnostics;

    const auto inline_payload
        = codec::serialize_runtime_path_state(state, nullptr, diagnostics);
    assert(inline_payload);
    const auto inline_decoded = codec::deserialize_runtime_path_state(
        *inline_payload, "shared-operation-inline", nullptr, diagnostics);
    assert(inline_decoded);
    assert_instance_contexts(*inline_decoded);

    const auto paths = make_external_table();
    const auto external_payload = codec::serialize_runtime_path_state(
        state, &paths, diagnostics);
    assert(external_payload);
    for (const auto path : {
             "top.control_context", "top.access_context",
             "top.process_control_context", "top.process_access_context",
             "top.sibling_control_context",
             "top.sibling_access_context" }) {
        assert(external_payload->find(path) == std::string::npos);
    }
    const auto external_decoded = codec::deserialize_runtime_path_state(
        *external_payload, "shared-operation-external", &paths,
        diagnostics);
    assert(external_decoded);
    assert_instance_contexts(*external_decoded);
}

void test_container_alias_state_roundtrips_and_validation()
{
    const auto state = make_alias_validation_state();
    fsim::diagnostic::Engine diagnostics;

    const auto inline_payload
        = codec::serialize_runtime_path_state(state, nullptr, diagnostics);
    assert(inline_payload);
    const auto inline_decoded = codec::deserialize_runtime_path_state(
        *inline_payload, "container-alias-inline", nullptr, diagnostics);
    assert(inline_decoded);
    assert(inline_decoded->container_element_signal_aliases
        == state.container_element_signal_aliases);
    assert(inline_decoded->container_aggregate_signal_aliases
        == state.container_aggregate_signal_aliases);
    const auto validated_inline
        = fsim::elaboration::ElaboratedDesign::from_state(*inline_decoded);
    assert(validated_inline);
    assert(validated_inline->container_element_signal_aliases()
        == state.container_element_signal_aliases);
    assert(validated_inline->container_aggregate_signal_aliases()
        == state.container_aggregate_signal_aliases);
    const auto aggregate_handle = validated_inline->find_signal("top.array");
    assert(aggregate_handle && *aggregate_handle == 2U);
    assert(validated_inline->signal_driver_inventory().has_value());
    const auto expected_inventory
        = *validated_inline->signal_driver_inventory();
    assert(expected_inventory.signals.size() == state.signals.size());
    const auto owned_signal = std::find_if(
        expected_inventory.signals.begin(), expected_inventory.signals.end(),
        [](const auto& signal) {
            return signal.storage_class
                == fsim::runtime::simir::SignalDriverStorageClass::single_owner;
        });
    assert(owned_signal != expected_inventory.signals.end());
    assert(owned_signal->owner_count == 1U);
    const auto owned_signal_index = static_cast<std::size_t>(
        owned_signal - expected_inventory.signals.begin());

    fsim::diagnostic::Engine valid_artifact_diagnostics;
    const auto valid_artifact_payload = codec::serialize_runtime_path_state(
        validated_inline->state(), nullptr, valid_artifact_diagnostics);
    assert(valid_artifact_payload);
    const auto valid_artifact = fsim::app::deserialize_runtime_state(
        *valid_artifact_payload, "signal-driver-inventory-roundtrip",
        valid_artifact_diagnostics);
    assert(valid_artifact);
    assert(valid_artifact->signal_driver_inventory()
        == validated_inline->signal_driver_inventory());

    auto mutable_corruption = validated_inline->state();
    assert(mutable_corruption.signal_driver_inventory.has_value());
    auto& corrupted_proof = *mutable_corruption.signal_driver_inventory;
    auto& corrupted_signal
        = corrupted_proof.signals[owned_signal_index];
    const auto& corrupted_owner
        = corrupted_proof.owners[corrupted_signal.first_owner];
    corrupted_proof.owner_mask_words[corrupted_owner.first_mask_word] ^= 1U;
    const auto repaired = fsim::elaboration::ElaboratedDesign::from_state(
        mutable_corruption);
    assert(repaired);
    assert(repaired->signal_driver_inventory() ==
        validated_inline->signal_driver_inventory());

    const auto strict_artifact_rejects = [&](const auto& mutate) {
        auto untrusted_state = validated_inline->state();
        assert(untrusted_state.signal_driver_inventory.has_value());
        mutate(*untrusted_state.signal_driver_inventory);
        fsim::diagnostic::Engine artifact_diagnostics;
        const auto payload = codec::serialize_runtime_path_state(
            untrusted_state, nullptr, artifact_diagnostics);
        assert(payload);
        const auto strict_artifact = fsim::app::deserialize_runtime_state(
            *payload, "corrupt-signal-driver-inventory",
            artifact_diagnostics);
        assert(!strict_artifact);
    };
    strict_artifact_rejects([&](auto& inventory) {
        inventory.signals[owned_signal_index].storage_class
            = fsim::runtime::simir::SignalDriverStorageClass::resolved_table;
    });
    strict_artifact_rejects([&](auto& inventory) {
        auto& signal = inventory.signals[owned_signal_index];
        auto& owner = inventory.owners[signal.first_owner];
        owner.process = static_cast<fsim::runtime::simir::ProcessId>(
            inventory.process_count);
    });
    strict_artifact_rejects([&](auto& inventory) {
        auto& signal = inventory.signals[owned_signal_index];
        auto& owner = inventory.owners[signal.first_owner];
        inventory.owner_mask_words[owner.first_mask_word] ^= 1U;
    });
    strict_artifact_rejects([&](auto& inventory) {
        auto& signal = inventory.signals[owned_signal_index];
        auto& owner = inventory.owners[signal.first_owner];
        inventory.owner_mask_words[owner.first_mask_word]
            |= UINT64_C(1) << signal.width;
    });

    const auto external = make_external_table();
    const auto external_payload = codec::serialize_runtime_path_state(
        state, &external, diagnostics);
    assert(external_payload);
    const auto external_decoded = codec::deserialize_runtime_path_state(
        *external_payload, "container-alias-external", &external,
        diagnostics);
    assert(external_decoded);
    assert(external_decoded->container_element_signal_aliases
        == state.container_element_signal_aliases);
    assert(external_decoded->container_aggregate_signal_aliases
        == state.container_aggregate_signal_aliases);
    assert(fsim::elaboration::ElaboratedDesign::from_state(
        *external_decoded));

    const auto first_leaf
        = inline_decoded->container_element_signal_aliases.front().signal;
    auto incomplete_alias_map = *inline_decoded;
    incomplete_alias_map.container_element_signal_aliases.pop_back();
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(incomplete_alias_map)));

    auto missing_aggregate_handle = *inline_decoded;
    missing_aggregate_handle.container_aggregate_signal_aliases.clear();
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(missing_aggregate_handle)));

    auto unreadable_leaf = *inline_decoded;
    unreadable_leaf.container_element_signal_aliases.front().readable = false;
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(unreadable_leaf)));

    auto unwritable_leaf = *inline_decoded;
    unwritable_leaf.container_element_signal_aliases.front().writable = false;
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(unwritable_leaf)));

    auto different_resolution = *inline_decoded;
    different_resolution.signals[first_leaf].resolution
        = fsim::runtime::simir::ResolutionKind::sv_wand;
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(different_resolution)));

    auto different_value_kind = *inline_decoded;
    different_value_kind.signals[first_leaf].value_kind
        = fsim::runtime::simir::ValueKind::logic9;
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(different_value_kind)));

    auto different_dimensions = *inline_decoded;
    auto& container_info
        = different_dimensions.container_object_info.front();
    container_info.type.index_left = 0;
    container_info.type.index_right = 1;
    container_info.type.dimensions = { { 0, 1 } };
    different_dimensions.container_objects.front().initial_value.type
        = container_info.type;
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(different_dimensions)));

    auto different_signal_name = *inline_decoded;
    different_signal_name.signals[first_leaf].name = "top.other_array[1]";
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(different_signal_name)));
}

void test_inline_and_external_roundtrips()
{
    auto state = make_state();
    fsim::diagnostic::Engine diagnostics;
    const auto standalone
        = codec::serialize_runtime_path_state(state, nullptr, diagnostics);
    assert(standalone);
    assert(standalone->starts_with("FSIMRUN1"));
    assert(static_cast<unsigned char>((*standalone)[12U]) == 0U);
    assert(standalone->find("top.signal")
        != std::string::npos);
    assert(standalone->find("top.signal", standalone->find("top.signal") + 1U)
        == std::string::npos);
    const auto checksum = codec::runtime_path_state_checksum(
        state, nullptr, diagnostics);
    assert(checksum);
    assert(*checksum == fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(*standalone)));

    const auto decoded = codec::deserialize_runtime_path_state(
        *standalone, "runtime-inline", nullptr, diagnostics);
    assert(decoded);
    assert_decoded_paths(*decoded);
    std::istringstream input { *standalone };
    const auto streamed = codec::deserialize_runtime_path_state(
        input, standalone->size(), "runtime-inline-stream", nullptr,
        diagnostics);
    assert(streamed);
    assert_decoded_paths(*streamed);

    const auto external = make_external_table();
    const auto enclosed = codec::serialize_runtime_path_state(
        state, &external, diagnostics);
    assert(enclosed);
    assert(static_cast<unsigned char>((*enclosed)[12U]) == 1U);
    assert(enclosed->find("top.signal") == std::string::npos);
    const auto external_decoded = codec::deserialize_runtime_path_state(
        *enclosed, "runtime-external", &external, diagnostics);
    assert(external_decoded);
    assert_decoded_paths(*external_decoded);

    std::ostringstream output;
    assert(codec::serialize_runtime_path_state(
        state, &external, output, diagnostics));
    assert(output.str() == *enclosed);

    HierarchyPathTable::Builder missing_builder;
    (void)missing_builder.intern("top");
    const auto missing = std::move(missing_builder).freeze();
    assert(!codec::serialize_runtime_path_state(
        state, &missing, diagnostics));

    const auto noncanonical = make_external_table(false);
    assert(!codec::serialize_runtime_path_state(
        state, &noncanonical, diagnostics));
}

void test_required_empty_and_bad_reference_rejections()
{
    fsim::diagnostic::Engine diagnostics;
    auto state = make_state();
    auto empty_required = state;
    empty_required.processes.front().name.clear();
    assert(!codec::serialize_runtime_path_state(
        empty_required, nullptr, diagnostics));

    const auto valid
        = codec::serialize_runtime_path_state(state, nullptr, diagnostics);
    assert(valid);
    // Magic, schema and path mode precede the binding; as a string-table
    // entry it starts with reference 0, then its length.
    constexpr std::size_t kBindingLengthOffset = 21U;
    constexpr std::size_t kBindingBytesOffset = 29U;
    const auto binding_size = static_cast<std::size_t>(
        read_u64(*valid, kBindingLengthOffset));
    const auto first_reference = kBindingBytesOffset + binding_size + 8U;
    assert(first_reference + 4U <= valid->size());
    const auto path_count = static_cast<std::size_t>(read_u64(
        *valid, kBindingBytesOffset + binding_size));
    // The cleared top path is a new string-table entry: reference 0, then
    // its length.
    const auto state_offset = first_reference + path_count * 4U;
    assert(state_offset + 16U <= valid->size());
    assert(read_u64(*valid, state_offset) == 0U);
    assert(read_u64(*valid, state_offset + 8U) == 0U);

    auto nonempty_placeholder = valid->substr(0U, state_offset + 8U);
    nonempty_placeholder.resize(state_offset + 17U);
    write_u64(nonempty_placeholder, state_offset + 8U, 1U);
    nonempty_placeholder[state_offset + 16U] = 'x';
    nonempty_placeholder.append(valid->substr(state_offset + 16U));
    assert(!codec::deserialize_runtime_path_state(
        nonempty_placeholder, "runtime-nonempty-placeholder", nullptr,
        diagnostics));

    auto bad_reference = *valid;
    write_u32(bad_reference, first_reference,
        std::numeric_limits<std::uint32_t>::max());
    assert(!codec::deserialize_runtime_path_state(
        bad_reference, "runtime-invalid-reference", nullptr, diagnostics));

    const auto table = path_codec::decode_hierarchy_paths(
        std::string_view { *valid }.substr(
            kBindingBytesOffset, binding_size));
    assert(table);
    HierarchyPathTable::Builder with_empty_builder;
    (void)with_empty_builder.intern("");
    for (std::size_t index = 0; index < table.payload->paths.size(); ++index) {
        const auto id = fsim::semantic::HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index));
        (void)with_empty_builder.intern(table.payload->paths.view(id));
    }
    const auto with_empty = std::move(with_empty_builder).freeze();
    const auto malformed_table
        = path_codec::encode_inline_hierarchy_paths(with_empty);
    assert(malformed_table);
    const auto bad_empty_path
        = replace_inline_table(*valid, malformed_table.payload->bytes);
    assert(!codec::deserialize_runtime_path_state(
        bad_empty_path, "runtime-empty-path-entry", nullptr, diagnostics));
}

void test_external_binding_mismatch()
{
    auto state = make_state();
    const auto external = make_external_table();
    fsim::diagnostic::Engine diagnostics;
    auto encoded = codec::serialize_runtime_path_state(
        state, &external, diagnostics);
    assert(encoded);
    const auto noncanonical = make_external_table(false);
    assert(!codec::deserialize_runtime_path_state(
        *encoded, "runtime-noncanonical-external-table", &noncanonical,
        diagnostics));
    constexpr std::size_t kBindingBytesOffset = 21U;
    assert(encoded->size() > kBindingBytesOffset);
    (*encoded)[kBindingBytesOffset] = (*encoded)[kBindingBytesOffset] == '0'
        ? '1'
        : '0';
    assert(!codec::deserialize_runtime_path_state(
        *encoded, "runtime-binding-mismatch", &external, diagnostics));
}

void test_decode_allocation_budget_is_bounded()
{
    auto state = make_state();
    fsim::diagnostic::Engine diagnostics;
    const auto valid
        = codec::serialize_runtime_path_state(state, nullptr, diagnostics);
    assert(valid);

    // Magic, schema and path mode precede the binding; as a string-table
    // entry it starts with reference 0, then its length.
    constexpr std::size_t kBindingLengthOffset = 21U;
    constexpr std::size_t kBindingBytesOffset = 29U;
    const auto binding_size = static_cast<std::size_t>(
        read_u64(*valid, kBindingLengthOffset));
    const auto path_count_offset = kBindingBytesOffset + binding_size;
    const auto path_count = static_cast<std::size_t>(
        read_u64(*valid, path_count_offset));
    const auto state_offset = path_count_offset + 8U + path_count * 4U;

    auto signal_info_count_offset = state_offset;
    // A string is a table reference; reference 0 adds a length and bytes.
    const auto skip_string = [&] {
        const auto reference = read_u64(*valid, signal_info_count_offset);
        signal_info_count_offset += 8U;
        if (reference == 0U) {
            signal_info_count_offset += 8U + static_cast<std::size_t>(
                read_u64(*valid, signal_info_count_offset));
        }
    };
    skip_string();
    const auto root_count = static_cast<std::size_t>(
        read_u64(*valid, signal_info_count_offset));
    signal_info_count_offset += 8U;
    for (std::size_t index = 0U; index < root_count; ++index) {
        skip_string();
    }
    assert(signal_info_count_offset + 8U <= valid->size());

    auto oversized = *valid;
    const auto count = codec::kMaximumDecodeAllocationBytes
        / sizeof(fsim::elaboration::SignalInfo) + 1U;
    write_u64(oversized, signal_info_count_offset, count);
    std::istringstream input { oversized };
    const auto rejected = codec::deserialize_runtime_path_state(
        input, codec::kMaximumPayloadBytes, "runtime-over-budget", nullptr,
        diagnostics);
    assert(!rejected);
}

} // namespace

int main()
{
    test_inline_and_external_roundtrips();
    test_container_alias_state_roundtrips_and_validation();
    test_shared_operation_context_path_projection();
    test_required_empty_and_bad_reference_rejections();
    test_external_binding_mismatch();
    test_decode_allocation_budget_is_bounded();
}
