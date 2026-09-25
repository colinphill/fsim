// SPDX-License-Identifier: Apache-2.0
#include "application_design_artifact_codec_internal.hpp"
#include "application_hierarchy_path_codec.hpp"
#include "application_internal.hpp"
#include "application_runtime_path_codec.hpp"
#include "fsim/support/path.hpp"

#include <filesystem>

namespace fsim::app {
namespace codec_detail {

#define FSIM_DEFINE_OPERATION_GROUP_CODEC(Name)                         \
    void write_operation_group(Writer& writer,                         \
        const runtime::simir::Name##OperationGroup& group)             \
    { writer.write(group.storage); }                                   \
    bool read_operation_group(Reader& reader,                          \
        runtime::simir::Name##OperationGroup& group)                   \
    { return reader.read(group.storage); }

FSIM_DEFINE_OPERATION_GROUP_CODEC(Value)
FSIM_DEFINE_OPERATION_GROUP_CODEC(Signal)
FSIM_DEFINE_OPERATION_GROUP_CODEC(String)
FSIM_DEFINE_OPERATION_GROUP_CODEC(Container)
FSIM_DEFINE_OPERATION_GROUP_CODEC(File)
FSIM_DEFINE_OPERATION_GROUP_CODEC(Scheduling)
FSIM_DEFINE_OPERATION_GROUP_CODEC(Control)
FSIM_DEFINE_OPERATION_GROUP_CODEC(Output)
FSIM_DEFINE_OPERATION_GROUP_CODEC(Class)

#undef FSIM_DEFINE_OPERATION_GROUP_CODEC

namespace {
template <typename Group>
bool select_operation_group(
    Reader& reader, runtime::simir::Operation& operation)
{
    return read_operation_group(
        reader, runtime::simir::operation_emplace_group<Group>(operation));
}
} // namespace

void write_operation(
    Writer& writer, const runtime::simir::Operation& operation)
{
    writer.u64(runtime::simir::operation_group_index(operation));
    runtime::simir::visit_operation(
        [&](const auto& value) {
            using Group = runtime::simir::OperationGroupFor<decltype(value)>;
            const auto* group
                = runtime::simir::operation_group_if<Group>(&operation);
            write_operation_group(writer, *group);
        },
        operation);
}

bool read_operation(Reader& reader, runtime::simir::Operation& operation)
{
    std::uint64_t selected { };
    if (!reader.u64(selected))
        return false;
    switch (selected) {
    case 0: return select_operation_group<runtime::simir::ValueOperationGroup>(reader, operation);
    case 1: return select_operation_group<runtime::simir::SignalOperationGroup>(reader, operation);
    case 2: return select_operation_group<runtime::simir::StringOperationGroup>(reader, operation);
    case 3:
        return select_operation_group<runtime::simir::ContainerOperationGroup>(
            reader, operation);
    case 4: return select_operation_group<runtime::simir::FileOperationGroup>(reader, operation);
    case 5:
        return select_operation_group<runtime::simir::SchedulingOperationGroup>(
            reader, operation);
    case 6: return select_operation_group<runtime::simir::ControlOperationGroup>(reader, operation);
    case 7: return select_operation_group<runtime::simir::OutputOperationGroup>(reader, operation);
    case 8: return select_operation_group<runtime::simir::ClassOperationGroup>(reader, operation);
    default: return reader.invalid_variant();
    }
}

bool portable_semantics(
    const semantic::ModelRecords& records,
    diagnostic::Engine& diagnostics)
{
    for (const auto& file : records.source_files) {
        if (support::path_is_portably_absolute(
                support::path_from_utf8(file.physical_name))) {
            diagnostics.error(
                std::string { kCode },
                "semantic state contains a producer-absolute source path: "
                    + file.physical_name);
            return false;
        }
    }
    return true;
}

}  // namespace codec_detail


using namespace codec_detail;

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_RUNTIME)
std::optional<std::string> serialize_runtime_state(
    const elaboration::ElaboratedDesign& design,
    diagnostic::Engine& diagnostics)
{
    return runtime_path_codec::serialize_runtime_path_state(
        design.state(), nullptr, diagnostics);
}

std::optional<std::string> serialize_runtime_state(
    const elaboration::ElaboratedDesign& design,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics)
{
    return runtime_path_codec::serialize_runtime_path_state(
        design.state(), &paths, diagnostics);
}

std::optional<std::string> serialize_runtime_state(
    elaboration::ElaboratedDesign&& design,
    diagnostic::Engine& diagnostics)
{
    return runtime_path_codec::serialize_runtime_path_state(
        std::move(design).state(), nullptr, diagnostics);
}

std::optional<std::string> serialize_runtime_state(
    elaboration::ElaboratedDesign&& design,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics)
{
    return runtime_path_codec::serialize_runtime_path_state(
        std::move(design).state(), &paths, diagnostics);
}

std::optional<std::string> runtime_state_checksum(
    const elaboration::ElaboratedDesignState& state,
    diagnostic::Engine& diagnostics)
{
    return runtime_path_codec::runtime_path_state_checksum(
        state, nullptr, diagnostics);
}

std::optional<std::string> runtime_state_checksum(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics)
{
    return runtime_path_codec::runtime_path_state_checksum(
        state, &paths, diagnostics);
}

bool serialize_runtime_state(
    const elaboration::ElaboratedDesignState& state,
    std::ostream& output,
    diagnostic::Engine& diagnostics)
{
    return runtime_path_codec::serialize_runtime_path_state(
        state, nullptr, output, diagnostics);
}

bool serialize_runtime_state(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable& paths,
    std::ostream& output,
    diagnostic::Engine& diagnostics)
{
    return runtime_path_codec::serialize_runtime_path_state(
        state, &paths, output, diagnostics);
}

std::optional<elaboration::ElaboratedDesign> deserialize_runtime_state(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics)
{
    auto state = runtime_path_codec::deserialize_runtime_path_state(
        bytes, std::move(source_name), nullptr, diagnostics);
    if (!state) {
        return std::nullopt;
    }
    auto design = elaboration::ElaboratedDesign::from_state(std::move(*state));
    if (!design) {
        diagnostics.error(
            std::string { kCode },
            "runtime state is structurally invalid");
        return std::nullopt;
    }
    return design;
}

std::optional<elaboration::ElaboratedDesign> deserialize_runtime_state(
    const std::string_view bytes,
    std::string source_name,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics)
{
    auto state = runtime_path_codec::deserialize_runtime_path_state(
        bytes, std::move(source_name), &paths, diagnostics);
    if (!state) {
        return std::nullopt;
    }
    auto design = elaboration::ElaboratedDesign::from_state(
        std::move(*state));
    if (!design) {
        diagnostics.error(std::string { kCode },
            "runtime state is structurally invalid");
    }
    return design;
}

std::optional<elaboration::ElaboratedDesign> deserialize_runtime_state(
    std::istream& input,
    const std::uint64_t size,
    std::string source_name,
    diagnostic::Engine& diagnostics)
{
    auto state = runtime_path_codec::deserialize_runtime_path_state(
        input, size, std::move(source_name), nullptr, diagnostics);
    if (!state) {
        return std::nullopt;
    }
    auto design = elaboration::ElaboratedDesign::from_state(std::move(*state));
    if (!design) {
        diagnostics.error(
            std::string { kCode },
            "runtime state is structurally invalid");
        return std::nullopt;
    }
    return design;
}

std::optional<elaboration::ElaboratedDesign> deserialize_runtime_state(
    std::istream& input,
    const std::uint64_t size,
    std::string source_name,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics)
{
    auto state = runtime_path_codec::deserialize_runtime_path_state(
        input, size, std::move(source_name), &paths, diagnostics);
    if (!state) {
        return std::nullopt;
    }
    auto design = elaboration::ElaboratedDesign::from_state(
        std::move(*state));
    if (!design) {
        diagnostics.error(std::string { kCode },
            "runtime state is structurally invalid");
    }
    return design;
}

#endif

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_COVERAGE_UVM)
std::optional<std::string> serialize_systemverilog_coverage_state(
    const runtime::SystemVerilogCoverageState& state,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics)
{
    runtime::SystemVerilogCoverageState validated;
    std::string error;
    if (!application_detail::restore_systemverilog_coverage_state(
            validated, state, hir, semantics, error)) {
        diagnostics.error(
            std::string { kCode },
            "coverage state is inconsistent with compiled HIR: " + error);
        return std::nullopt;
    }
    return serialize(
        "FSIMSVCV", kSystemVerilogCoverageStateSchema, validated, diagnostics);
}

std::optional<runtime::SystemVerilogCoverageState>
deserialize_systemverilog_coverage_state(
    const std::string_view bytes,
    std::string source_name,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics)
{
    auto decoded = deserialize<runtime::SystemVerilogCoverageState>(
        "FSIMSVCV", kSystemVerilogCoverageStateSchema,
        bytes, std::move(source_name), diagnostics);
    if (!decoded) {
        return std::nullopt;
    }
    runtime::SystemVerilogCoverageState state;
    std::string error;
    if (!application_detail::restore_systemverilog_coverage_state(
            state, *decoded, hir, semantics, error)) {
        diagnostics.error(
            std::string { kCode },
            "coverage state is inconsistent with compiled HIR: " + error);
        return std::nullopt;
    }
    return state;
}

std::optional<std::string> serialize_systemverilog_uvm_state(
    const runtime::SystemVerilogUvmCheckpointArtifact& state,
    diagnostic::Engine& diagnostics)
{
    if (runtime::validate_systemverilog_uvm_checkpoint(
            state, state.provenance)
        != runtime::SystemVerilogUvmCheckpointError::None) {
        diagnostics.error(
            "FSIM-UVM-STATE-001",
            "SystemVerilog UVM state is structurally invalid or nonportable");
        return std::nullopt;
    }
    return serialize(
        "FSIMUVM1", kSystemVerilogUvmStateSchema, state, diagnostics);
}

std::optional<runtime::SystemVerilogUvmCheckpointArtifact>
deserialize_systemverilog_uvm_state(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics)
{
    auto state = deserialize<runtime::SystemVerilogUvmCheckpointArtifact>(
        "FSIMUVM1", kSystemVerilogUvmStateSchema, bytes,
        std::move(source_name), diagnostics);
    if (!state) {
        return std::nullopt;
    }
    if (runtime::validate_systemverilog_uvm_checkpoint(
            *state, state->provenance)
        != runtime::SystemVerilogUvmCheckpointError::None) {
        diagnostics.error(
            "FSIM-UVM-STATE-001",
            "SystemVerilog UVM state is structurally invalid or nonportable");
        return std::nullopt;
    }
    return state;
}

#endif

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_SEMANTIC)
namespace codec_detail {

std::optional<std::string> serialize_semantic_records(
    semantic::ModelRecords records,
    diagnostic::Engine& diagnostics)
{
    if (!portable_semantics(records, diagnostics)) {
        return std::nullopt;
    }
    return serialize(
        "FSIMSEM1", kSemanticStateSchema, records, diagnostics);
}

std::optional<std::string> serialize_validated_semantic_state(
    const semantic::Model& model,
    diagnostic::Engine& diagnostics)
{
    return serialize_semantic_records(model.records(), diagnostics);
}

} // namespace codec_detail

std::optional<std::string> serialize_semantic_state(
    const semantic::Model& model,
    diagnostic::Engine& diagnostics)
{
    auto records = model.records();
    if (!model.valid()) {
        return std::nullopt;
    }
    return codec_detail::serialize_semantic_records(
        std::move(records), diagnostics);
}

namespace application_detail {

std::optional<std::string>
serialize_cache_semantic_state_with_source_projection(
    const semantic::Model& semantics,
    const std::span<const library::SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics)
{
    auto records = project_compiled_semantic_source_names(
        semantics, mappings, diagnostics);
    if (!records) {
        return std::nullopt;
    }
    return codec_detail::serialize_semantic_records(
        std::move(*records), diagnostics);
}

} // namespace application_detail

std::optional<semantic::Model> deserialize_semantic_state(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics)
{
    return translate_decode_allocation_failure<semantic::Model>(
        source_name, diagnostics, [&] {
            DecodeBudget budget;
            return deserialize_semantic_state_with_budget(
                bytes, source_name, diagnostics, budget);
        });
}

namespace codec_detail {

std::optional<semantic::Model> deserialize_semantic_state_with_budget(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics,
    DecodeBudget& budget)
{
    auto records = deserialize<semantic::ModelRecords>(
        "FSIMSEM1", kSemanticStateSchema, bytes, std::move(source_name),
        diagnostics, &budget);
    if (!records || !portable_semantics(*records, diagnostics)) {
        return std::nullopt;
    }
    auto model = semantic::Model::from_records(std::move(*records));
    if (!model) {
        diagnostics.error(std::string { kCode }, "semantic state is structurally invalid");
    }
    return model;
}

} // namespace codec_detail

#endif

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_DESIGN_IR)
namespace {

constexpr std::uint8_t kInlinePaths = 0U;
constexpr std::uint8_t kExternalPaths = 1U;
constexpr std::size_t kHierarchyPathHeaderBytes = 8U + 4U + 4U;

[[nodiscard]] bool read_hierarchy_path_u32(
    const std::string_view bytes,
    std::size_t& position,
    std::uint32_t& value) noexcept
{
    if (position > bytes.size() || bytes.size() - position < 4U) {
        return false;
    }
    value = 0U;
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        value |= static_cast<std::uint32_t>(
            static_cast<unsigned char>(bytes[position++])) << shift;
    }
    return true;
}

[[nodiscard]] bool hierarchy_path_byte_less(
    const std::string_view left,
    const std::string_view right) noexcept
{
    const auto common = std::min(left.size(), right.size());
    for (std::size_t index = 0U; index < common; ++index) {
        const auto left_byte = static_cast<unsigned char>(left[index]);
        const auto right_byte = static_cast<unsigned char>(right[index]);
        if (left_byte != right_byte) {
            return left_byte < right_byte;
        }
    }
    return left.size() < right.size();
}

[[nodiscard]] bool valid_hierarchy_path_text(
    const std::string_view path) noexcept
{
    const auto continuation = [](const unsigned char byte) {
        return byte >= 0x80U && byte <= 0xbfU;
    };
    for (std::size_t index = 0U; index < path.size();) {
        const auto first = static_cast<unsigned char>(path[index]);
        if (first == 0U) {
            return false;
        }
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            if (index + 1U >= path.size()
                || !continuation(
                    static_cast<unsigned char>(path[index + 1U]))) {
                return false;
            }
            index += 2U;
            continue;
        }
        if (first >= 0xe0U && first <= 0xefU) {
            if (index + 2U >= path.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(path[index + 1U]);
            const auto third = static_cast<unsigned char>(path[index + 2U]);
            if (!continuation(third)
                || (first == 0xe0U && (second < 0xa0U || second > 0xbfU))
                || (first == 0xedU && (second < 0x80U || second > 0x9fU))
                || (first != 0xe0U && first != 0xedU
                    && !continuation(second))) {
                return false;
            }
            index += 3U;
            continue;
        }
        if (first >= 0xf0U && first <= 0xf4U) {
            if (index + 3U >= path.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(path[index + 1U]);
            const auto third = static_cast<unsigned char>(path[index + 2U]);
            const auto fourth = static_cast<unsigned char>(path[index + 3U]);
            if (!continuation(third) || !continuation(fourth)
                || (first == 0xf0U && (second < 0x90U || second > 0xbfU))
                || (first == 0xf4U && (second < 0x80U || second > 0x8fU))
                || (first != 0xf0U && first != 0xf4U
                    && !continuation(second))) {
                return false;
            }
            index += 4U;
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool charge_inline_hierarchy_path_allocations(
    DecodeBudget& budget,
    const std::string_view bytes)
{
    const hierarchy_path_codec::Limits limits;
    if (bytes.size() > limits.maximum_payload_bytes
        || bytes.size() < kHierarchyPathHeaderBytes
        || !bytes.starts_with(hierarchy_path_codec::kMagic)) {
        return true;
    }

    std::size_t position = hierarchy_path_codec::kMagic.size();
    std::uint32_t schema { };
    std::uint32_t count { };
    if (!read_hierarchy_path_u32(bytes, position, schema)
        || !read_hierarchy_path_u32(bytes, position, count)
        || schema != hierarchy_path_codec::kSchema
        || count > limits.maximum_paths
        || count >= std::numeric_limits<std::uint32_t>::max()
        || count > (bytes.size() - position) / 4U) {
        return true;
    }
    if (!budget.consume(count, sizeof(std::string_view))) {
        return false;
    }

    std::size_t total_path_bytes { };
    std::string_view previous;
    for (std::uint32_t index = 0U; index < count; ++index) {
        std::uint32_t path_size { };
        if (!read_hierarchy_path_u32(bytes, position, path_size)
            || path_size > limits.maximum_path_bytes
            || path_size > bytes.size() - position
            || total_path_bytes > limits.maximum_total_path_bytes
            || path_size > limits.maximum_total_path_bytes - total_path_bytes) {
            return true;
        }
        const auto path = bytes.substr(position, path_size);
        if (!valid_hierarchy_path_text(path)
            || (index != 0U && !hierarchy_path_byte_less(previous, path))) {
            return true;
        }
        total_path_bytes += path_size;
        position += path_size;
        previous = path;
    }
    if (position != bytes.size()) {
        return true;
    }

    constexpr std::size_t path_entry_bytes = sizeof(std::string)
        + sizeof(std::string_view) + sizeof(semantic::HierarchyPathId)
        + 4U * sizeof(void*);
    return budget.consume(bytes.size(), sizeof(char))
        && budget.consume(count, path_entry_bytes)
        && budget.consume(total_path_bytes, sizeof(char))
        && budget.consume(1U, sizeof(std::string) + 8U * sizeof(void*))
        && budget.consume(65U, sizeof(char));
}

[[nodiscard]] bool same_path_ids(
    const semantic::HierarchyPathTable& left,
    const semantic::HierarchyPathTable& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto id = semantic::HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index));
        if (left.view(id) != right.view(id)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool required_design_ir_paths_nonempty(
    const semantic::design::DesignIr& design)
{
    if (design.top_path_id().valid() && design.top().empty()) {
        return false;
    }
    if ((!design.roots().empty() || !design.instances().empty()
            || !design.objects().empty() || !design.conversions().empty()
            || !design.boundaries().empty())
        && design.top().empty()) {
        return false;
    }
    const auto has_path = [&](const semantic::HierarchyPathId id) {
        return id.valid()
            && id.value() < design.hierarchy_paths().size()
            && !design.path(id).empty();
    };
    const auto records_have_paths = [&](const auto& records) {
        return std::ranges::all_of(records,
            [&](const auto& record) { return has_path(record.path); });
    };
    return std::ranges::all_of(design.roots(), has_path)
        && records_have_paths(design.instances())
        && records_have_paths(design.objects())
        && records_have_paths(design.conversions())
        && records_have_paths(design.boundaries());
}

[[nodiscard]] std::optional<DesignIrRecords> project_design_ir_records(
    const semantic::design::DesignIr& design,
    const semantic::HierarchyPathTable& target,
    std::uint8_t path_mode,
    std::string path_binding,
    diagnostic::Engine& diagnostics)
{
    DesignIrRecords records {
        path_mode, std::move(path_binding), { }, { },
        design.specializations(), design.instances(), design.objects(),
        design.ports(), design.processes(), design.sensitivities(),
        design.drivers(), design.transactions(), design.conversions(),
        design.boundaries()
    };
    const auto& source = design.hierarchy_paths();
    const auto remap = [&](semantic::HierarchyPathId& id) {
        const auto mapped = hierarchy_path_codec::remap_path_reference(
            source, id, target);
        if (!mapped) {
            return false;
        }
        id = *mapped;
        return true;
    };
    if (design.top_path_id().valid()) {
        records.top = design.top_path_id();
        if (!remap(records.top)) {
            diagnostics.error(std::string { kCode },
                "DesignIR top path is absent from the canonical table");
            return std::nullopt;
        }
    } else if (!design.roots().empty()) {
        diagnostics.error(std::string { kCode },
            "DesignIR top path is absent from its owner table");
        return std::nullopt;
    }
    for (const auto root : design.roots()) {
        records.roots.push_back(root);
        if (!remap(records.roots.back())) {
            diagnostics.error(std::string { kCode },
                "DesignIR root path is absent from the canonical table");
            return std::nullopt;
        }
    }
    const auto remap_records = [&](auto& items) {
        for (auto& item : items) {
            if (!remap(item.path)) {
                return false;
            }
        }
        return true;
    };
    if (!remap_records(records.instances)
        || !remap_records(records.objects)
        || !remap_records(records.conversions)
        || !remap_records(records.boundaries)) {
        diagnostics.error(std::string { kCode },
            "DesignIR path is absent from the canonical table");
        return std::nullopt;
    }
    return records;
}

[[nodiscard]] std::optional<std::string> serialize_design_ir_with_paths(
    const semantic::design::DesignIr& design,
    const semantic::HierarchyPathTable* const external_paths,
    diagnostic::Engine& diagnostics)
{
    if (!design.valid() || !required_design_ir_paths_nonempty(design)) {
        diagnostics.error(std::string { kCode },
            "cannot serialize invalid DesignIR");
        return std::nullopt;
    }
    const auto& source = external_paths != nullptr
        ? *external_paths : design.hierarchy_paths();
    auto payload = hierarchy_path_codec::encode_inline_hierarchy_paths(source);
    if (!payload) {
        diagnostics.error(std::string { kCode },
            "cannot encode the DesignIR hierarchy path table");
        return std::nullopt;
    }
    if (external_paths != nullptr
        && !same_path_ids(*external_paths, payload.payload->paths)) {
        diagnostics.error(std::string { kCode },
            "external DesignIR hierarchy path IDs are not canonical");
        return std::nullopt;
    }
    const auto mode = external_paths != nullptr
        ? kExternalPaths : kInlinePaths;
    auto binding = external_paths != nullptr
        ? payload.payload->digest : payload.payload->bytes;
    auto records = project_design_ir_records(
        design, payload.payload->paths, mode, std::move(binding),
        diagnostics);
    if (!records) {
        return std::nullopt;
    }
    return serialize(
        "FSIMDIR1", kDesignIrStateSchema, *records, diagnostics);
}

[[nodiscard]] std::optional<semantic::design::DesignIr>
deserialize_design_ir_with_paths(
    const std::string_view bytes,
    std::string source_name,
    const semantic::HierarchyPathTable* const external_paths,
    diagnostic::Engine& diagnostics)
{
    DecodeBudget budget { kCompiledHirDecodeBudgetBytes };
    auto records = deserialize<DesignIrRecords>(
        "FSIMDIR1", kDesignIrStateSchema, bytes, source_name,
        diagnostics, &budget);
    if (!records) {
        return std::nullopt;
    }
    semantic::HierarchyPathTable paths;
    if (records->path_mode == kInlinePaths
        && external_paths == nullptr) {
        if (!charge_inline_hierarchy_path_allocations(
                budget, records->path_binding)) {
            diagnostics.error(std::string { kCode },
                "design state exceeds the aggregate decode allocation budget",
                { std::move(source_name), { 1, 1, 0 }, { 1, 1, 0 } });
            return std::nullopt;
        }
        auto payload = hierarchy_path_codec::decode_hierarchy_paths(
            records->path_binding);
        if (!payload) {
            diagnostics.error(std::string { kCode },
                "DesignIR inline hierarchy path table is invalid");
            return std::nullopt;
        }
        paths = std::move(payload.payload->paths);
    } else if (records->path_mode == kExternalPaths
        && external_paths != nullptr) {
        auto payload = hierarchy_path_codec::encode_inline_hierarchy_paths(
            *external_paths);
        if (!payload
            || !same_path_ids(*external_paths, payload.payload->paths)
            || records->path_binding != payload.payload->digest) {
            diagnostics.error(std::string { kCode },
                "DesignIR external hierarchy path table digest is invalid");
            return std::nullopt;
        }
        paths = *external_paths;
    } else {
        diagnostics.error(std::string { kCode },
            "DesignIR hierarchy path table mode is invalid");
        return std::nullopt;
    }
    semantic::design::DesignIr design;
    design.mutable_top() = records->top;
    design.mutable_roots() = std::move(records->roots);
    design.mutable_specializations() = std::move(records->specializations);
    design.mutable_instances() = std::move(records->instances);
    design.mutable_objects() = std::move(records->objects);
    design.mutable_ports() = std::move(records->ports);
    design.mutable_processes() = std::move(records->processes);
    design.mutable_sensitivities() = std::move(records->sensitivities);
    design.mutable_drivers() = std::move(records->drivers);
    design.mutable_transactions() = std::move(records->transactions);
    design.mutable_conversions() = std::move(records->conversions);
    design.mutable_boundaries() = std::move(records->boundaries);
    if (!design.rebind_path_table(std::move(paths)) || !design.valid()
        || !required_design_ir_paths_nonempty(design)) {
        diagnostics.error(std::string { kCode },
            "DesignIR state has an invalid hierarchy path reference");
        return std::nullopt;
    }
    return design;
}

} // namespace

std::optional<std::string> serialize_design_ir_state(
    const semantic::design::DesignIr& design,
    diagnostic::Engine& diagnostics)
{
    return serialize_design_ir_with_paths(design, nullptr, diagnostics);
}

std::optional<std::string> serialize_design_ir_state(
    const semantic::design::DesignIr& design,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics)
{
    return serialize_design_ir_with_paths(design, &paths, diagnostics);
}

std::optional<semantic::design::DesignIr> deserialize_design_ir_state(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics)
{
    return deserialize_design_ir_with_paths(
        bytes, std::move(source_name), nullptr, diagnostics);
}

std::optional<semantic::design::DesignIr> deserialize_design_ir_state(
    const std::string_view bytes,
    std::string source_name,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics)
{
    return deserialize_design_ir_with_paths(
        bytes, std::move(source_name), &paths, diagnostics);
}

#endif

} // namespace fsim::app
