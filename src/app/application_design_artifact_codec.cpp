// SPDX-License-Identifier: Apache-2.0
#include "application_design_artifact_codec_internal.hpp"
#include "application_internal.hpp"

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

} // namespace codec_detail


using namespace codec_detail;

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_RUNTIME)
std::optional<std::string> serialize_runtime_state(
    const elaboration::ElaboratedDesign& design,
    diagnostic::Engine& diagnostics)
{
    return serialize(
        "FSIMRUN1", kRuntimeStateSchema, design.state(), diagnostics);
}

std::optional<std::string> serialize_runtime_state(
    elaboration::ElaboratedDesign&& design,
    diagnostic::Engine& diagnostics)
{
    return serialize("FSIMRUN1", kRuntimeStateSchema,
        std::move(design).state(), diagnostics);
}

std::optional<std::string> runtime_state_checksum(
    const elaboration::ElaboratedDesignState& state,
    diagnostic::Engine& diagnostics)
{
    return serialized_checksum(
        "FSIMRUN1", kRuntimeStateSchema, state, diagnostics);
}

bool serialize_runtime_state(
    const elaboration::ElaboratedDesignState& state,
    std::ostream& output,
    diagnostic::Engine& diagnostics)
{
    return serialize_to_stream(
        "FSIMRUN1", kRuntimeStateSchema, state, output, diagnostics);
}

std::optional<elaboration::ElaboratedDesign> deserialize_runtime_state(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics)
{
    auto state = deserialize<elaboration::ElaboratedDesignState>(
        "FSIMRUN1", kRuntimeStateSchema, bytes, std::move(source_name),
        diagnostics);
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
    diagnostic::Engine& diagnostics)
{
    auto state = deserialize<elaboration::ElaboratedDesignState>(
        "FSIMRUN1", kRuntimeStateSchema, input, size, std::move(source_name),
        diagnostics);
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
std::optional<std::string> serialize_semantic_state(
    const semantic::Model& model,
    diagnostic::Engine& diagnostics)
{
    auto records = model.records();
    if (!model.valid() || !portable_semantics(records, diagnostics)) {
        return std::nullopt;
    }
    return serialize(
        "FSIMSEM1", kSemanticStateSchema, records, diagnostics);
}

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
std::optional<std::string> serialize_design_ir_state(
    const semantic::design::DesignIr& design,
    diagnostic::Engine& diagnostics)
{
    if (!design.valid()) {
        diagnostics.error(std::string { kCode }, "cannot serialize invalid DesignIR");
        return std::nullopt;
    }
    const DesignIrRecords records {
        design.top(), design.roots(), design.specializations(),
        design.instances(), design.objects(), design.ports(), design.processes(),
        design.sensitivities(), design.drivers(), design.transactions(),
        design.conversions(), design.boundaries()
    };
    return serialize(
        "FSIMDIR1", kDesignIrStateSchema, records, diagnostics);
}

std::optional<semantic::design::DesignIr> deserialize_design_ir_state(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics)
{
    auto records = deserialize<DesignIrRecords>(
        "FSIMDIR1", kDesignIrStateSchema, bytes, std::move(source_name),
        diagnostics);
    if (!records) {
        return std::nullopt;
    }
    semantic::design::DesignIr design;
    design.mutable_top() = std::move(records->top);
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
    if (!design.valid()) {
        diagnostics.error(std::string { kCode }, "DesignIR state is structurally invalid");
        return std::nullopt;
    }
    return design;
}

#endif

} // namespace fsim::app
