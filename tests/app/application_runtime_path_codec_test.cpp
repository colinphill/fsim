// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_hierarchy_path_codec.hpp"
#include "../../src/app/application_runtime_path_codec.hpp"

#include "fsim/support/sha256.hpp"

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
    state.signals.emplace_back(
        std::string { kSignalPath }, fsim::runtime::PackedLogic4 { });

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

HierarchyPathTable make_external_table(const bool canonicalize = true)
{
    HierarchyPathTable::Builder builder;
    for (const auto path : {
             "zz.design_only", "top.signal", "top", "top.array",
             "top.string", "top.child.formal", "top.child.actual",
             "top.boundary", "top.protected", "top.process",
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
    constexpr std::size_t kBindingLengthOffset = 13U;
    constexpr std::size_t kBindingBytesOffset = 21U;
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
    assert(state.signal_names.front().first == "top.signal");
    assert(state.string_names.front().first == "top.string");
    assert(state.container_names.front().first == "top.array");
    assert(state.code_coverage_inventory->instances.front().instance
        == "top.coverage");
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
    constexpr std::size_t kBindingLengthOffset = 13U;
    constexpr std::size_t kBindingBytesOffset = 21U;
    const auto binding_size = static_cast<std::size_t>(
        read_u64(*valid, kBindingLengthOffset));
    const auto first_reference = kBindingBytesOffset + binding_size + 8U;
    assert(first_reference + 4U <= valid->size());
    const auto path_count = static_cast<std::size_t>(read_u64(
        *valid, kBindingBytesOffset + binding_size));
    const auto state_offset = first_reference + path_count * 4U;
    assert(state_offset + 8U <= valid->size());
    assert(read_u64(*valid, state_offset) == 0U);

    auto nonempty_placeholder = valid->substr(0U, state_offset);
    nonempty_placeholder.resize(state_offset + 9U);
    write_u64(nonempty_placeholder, state_offset, 1U);
    nonempty_placeholder[state_offset + 8U] = 'x';
    nonempty_placeholder.append(valid->substr(state_offset + 8U));
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

    constexpr std::size_t kBindingLengthOffset = 13U;
    constexpr std::size_t kBindingBytesOffset = 21U;
    const auto binding_size = static_cast<std::size_t>(
        read_u64(*valid, kBindingLengthOffset));
    const auto path_count_offset = kBindingBytesOffset + binding_size;
    const auto path_count = static_cast<std::size_t>(
        read_u64(*valid, path_count_offset));
    const auto state_offset = path_count_offset + 8U + path_count * 4U;

    auto signal_info_count_offset = state_offset;
    const auto top_size = static_cast<std::size_t>(
        read_u64(*valid, signal_info_count_offset));
    signal_info_count_offset += 8U + top_size;
    const auto root_count = static_cast<std::size_t>(
        read_u64(*valid, signal_info_count_offset));
    signal_info_count_offset += 8U;
    for (std::size_t index = 0U; index < root_count; ++index) {
        const auto root_size = static_cast<std::size_t>(
            read_u64(*valid, signal_info_count_offset));
        signal_info_count_offset += 8U + root_size;
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
    test_required_empty_and_bad_reference_rejections();
    test_external_binding_mismatch();
    test_decode_allocation_budget_is_bounded();
}
