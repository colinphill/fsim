// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_path_timing.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SdfConstructKind;
    using frontend::SdfIrNode;
    using frontend::SourceSpan;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] bool timing_check_kind(const SdfConstructKind kind) noexcept
    {
        static constexpr auto kinds = std::to_array<SdfConstructKind>({
            SdfConstructKind::Setup,
            SdfConstructKind::Hold,
            SdfConstructKind::SetupHold,
            SdfConstructKind::Recovery,
            SdfConstructKind::Removal,
            SdfConstructKind::RecRem,
            SdfConstructKind::Skew,
            SdfConstructKind::BidirectSkew,
            SdfConstructKind::Width,
            SdfConstructKind::Period,
        });
        return std::ranges::find(kinds, kind) != kinds.end();
    }

    [[nodiscard]] SdfDelayApplicationMode annotation_mode(
        const frontend::SdfIr& ir, const std::uint64_t node_id) noexcept
    {
        const auto* node = ir.find_node(node_id);
        while (node != nullptr && node->parent_id != 0U) {
            node = ir.find_node(node->parent_id);
            if (node == nullptr)
                return SdfDelayApplicationMode::None;
            if (node->kind == SdfConstructKind::Absolute)
                return SdfDelayApplicationMode::Absolute;
            if (node->kind == SdfConstructKind::Increment)
                return SdfDelayApplicationMode::Increment;
        }
        return SdfDelayApplicationMode::None;
    }

    [[nodiscard]] std::vector<const SdfIrNode*> value_nodes(
        const frontend::SdfIr& ir, const std::uint64_t root_id)
    {
        std::unordered_set<std::uint64_t> descendants { root_id };
        std::vector<const SdfIrNode*> result;
        for (const auto& node : ir.nodes()) {
            if (node.id != root_id && !descendants.contains(node.parent_id))
                continue;
            descendants.insert(node.id);
            if (node.exact_value)
                result.push_back(&node);
        }
        return result;
    }

    [[nodiscard]] const elaboration::SpecializationInfo* specialization_for(
        const elaboration::ElaboratedDesignState& state,
        const std::string_view instance)
    {
        const elaboration::SpecializationInfo* result = nullptr;
        for (const auto& specialization : state.specializations) {
            if (specialization.instance != instance
                || specialization.language != frontend::Language::Vhdl2008) {
                continue;
            }
            if (result != nullptr)
                return nullptr;
            result = &specialization;
        }
        return result;
    }

    [[nodiscard]] std::optional<std::uint64_t> constant_tick(
        const runtime::simir::Process& process,
        const runtime::simir::RegisterId target,
        const std::size_t before_instruction)
    {
        std::unordered_map<runtime::simir::RegisterId, runtime::PackedLogic4>
            values;
        for (std::size_t index = 0;
            index < before_instruction && index < process.operations.size();
            ++index) {
            const auto* load = runtime::simir::operation_get_if<
                runtime::simir::LoadConstant>(&process.operations[index]);
            if (load != nullptr) {
                values[load->destination] = load->value;
                continue;
            }
            const auto* extract = runtime::simir::operation_get_if<
                runtime::simir::Extract>(&process.operations[index]);
            if (extract == nullptr)
                continue;
            const auto source = values.find(extract->source);
            if (source == values.end()
                || static_cast<std::size_t>(extract->offset)
                        + extract->width
                    > source->second.width()) {
                values.erase(extract->destination);
                continue;
            }
            runtime::PackedLogic4 value(
                extract->width, runtime::Logic4::zero);
            for (std::size_t bit = 0; bit < extract->width; ++bit) {
                value.set(bit,
                    source->second.get(extract->offset + bit));
            }
            values[extract->destination] = std::move(value);
        }
        const auto found = values.find(target);
        if (found == values.end())
            return std::nullopt;
        const auto word = found->second.low_word();
        return word.bval == 0U ? std::optional { word.aval } : std::nullopt;
    }

    [[nodiscard]] std::optional<runtime::simir::SignalId> endpoint_signal(
        const SdfVitalPlannedTarget& target, const SdfEndpointRole role)
    {
        std::optional<runtime::simir::SignalId> result;
        for (const auto& port : target.ports) {
            if (port.role != role)
                continue;
            if (result)
                return std::nullopt;
            result = port.signal;
        }
        return result;
    }

    [[nodiscard]] bool timing_call_matches(
        const runtime::simir::VitalTimingCheck& operation,
        const SdfVitalPlannedTarget& target)
    {
        const auto data = endpoint_signal(target, SdfEndpointRole::TimingData);
        const auto reference
            = endpoint_signal(target, SdfEndpointRole::TimingReference);
        if (!data || operation.test_signal != *data)
            return false;
        return reference ? operation.reference_signal == reference : true;
    }

    [[nodiscard]] bool timing_kind_matches(
        const SdfConstructKind construct,
        const runtime::simir::VitalTimingCheckKind kind) noexcept
    {
        switch (construct) {
        case SdfConstructKind::Setup:
        case SdfConstructKind::Hold:
        case SdfConstructKind::SetupHold:
            return kind == runtime::simir::VitalTimingCheckKind::setup_hold;
        case SdfConstructKind::Recovery:
        case SdfConstructKind::Removal:
        case SdfConstructKind::RecRem:
            return kind
                == runtime::simir::VitalTimingCheckKind::recovery_removal;
        case SdfConstructKind::Width:
        case SdfConstructKind::Period:
            return kind == runtime::simir::VitalTimingCheckKind::period_pulse;
        case SdfConstructKind::Skew:
        case SdfConstructKind::BidirectSkew:
            return kind == runtime::simir::VitalTimingCheckKind::in_phase_skew
                || kind
                == runtime::simir::VitalTimingCheckKind::out_phase_skew;
        default:
            return false;
        }
    }

    struct CallCandidate {
        const runtime::simir::Process* process { };
        std::uint32_t instruction { };
        const runtime::simir::VitalDelay* delay { };
        const runtime::simir::VitalTimingCheck* check { };
    };

    [[nodiscard]] std::vector<CallCandidate> find_calls(
        const elaboration::ElaboratedDesignState& state,
        const elaboration::SpecializationInfo& specialization,
        const SdfVitalPlannedTarget& target)
    {
        std::vector<CallCandidate> result;
        const auto check_kind = timing_check_kind(target.construct_kind);
        const auto output = endpoint_signal(target, SdfEndpointRole::Output);
        for (const auto process_id : specialization.processes) {
            if (process_id >= state.processes.size())
                continue;
            const auto& process = state.processes[process_id];
            for (std::size_t index = 0; index < process.operations.size(); ++index) {
                if (index > std::numeric_limits<std::uint32_t>::max())
                    break;
                if (check_kind) {
                    const auto* check = runtime::simir::operation_get_if<
                        runtime::simir::VitalTimingCheck>(
                        &process.operations[index]);
                    if (check != nullptr && timing_call_matches(*check, target)) {
                        result.push_back({ &process,
                            static_cast<std::uint32_t>(index), nullptr, check });
                    }
                    continue;
                }
                const auto* delay = runtime::simir::operation_get_if<
                    runtime::simir::VitalDelay>(&process.operations[index]);
                if (delay != nullptr && output && delay->output == *output
                    && delay->kind == runtime::simir::VitalDelayKind::path) {
                    result.push_back({ &process,
                        static_cast<std::uint32_t>(index), delay, nullptr });
                }
            }
        }
        return result;
    }

    [[nodiscard]] std::size_t delay_count(
        const runtime::simir::VitalDelayShape shape) noexcept
    {
        switch (shape) {
        case runtime::simir::VitalDelayShape::single:
            return 1U;
        case runtime::simir::VitalDelayShape::delay01:
            return 2U;
        case runtime::simir::VitalDelayShape::delay01z:
            return 6U;
        }
        return 0U;
    }

    [[nodiscard]] bool before_values(const CallCandidate& candidate,
        SdfVitalPathTimingRecord& record,
        std::vector<Diagnostic>& diagnostics)
    {
        if (candidate.delay != nullptr) {
            const auto count = delay_count(candidate.delay->shape);
            for (std::size_t index = 0; index < count; ++index) {
                const auto value = constant_tick(*candidate.process,
                    candidate.delay->default_delays[index],
                    candidate.instruction);
                if (!value) {
                    diagnose(diagnostics, "FSIM-SDF-VITAL-PATH-005",
                        "VITAL path source delay is not a retained static tick value",
                        record.source);
                    return false;
                }
                record.before_delay_ticks.push_back(*value);
            }
            return true;
        }
        if (candidate.check != nullptr) {
            record.before_check_ticks.reserve(candidate.check->limits.size());
            for (const auto limit : candidate.check->limits) {
                if (limit > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                    diagnose(diagnostics, "FSIM-SDF-VITAL-PATH-005",
                        "VITAL timing-check source limit exceeds signed SDF range",
                        record.source);
                    return false;
                }
                record.before_check_ticks.push_back(
                    static_cast<std::int64_t>(limit));
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] bool after_values(const frontend::SdfIr& ir,
        const SdfVitalPlannedTarget& target, const SdfValuePolicy& policy,
        const SdfVitalPathTimingLimits& limits,
        SdfVitalPathTimingRecord& record,
        std::vector<Diagnostic>& diagnostics)
    {
        if (!ir.timescale()) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PATH-001",
                "SDF VITAL path planning requires a normalized timescale", { });
            return false;
        }
        const auto nodes = value_nodes(ir, target.node_id);
        if (nodes.empty() || nodes.size() > limits.max_values_per_record) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PATH-004",
                "SDF VITAL path/check value arity is empty or exceeds its limit",
                record.source);
            return false;
        }
        bool valid = true;
        for (const auto* node : nodes) {
            if (timing_check_kind(target.construct_kind)) {
                const auto selected = select_sdf_timing_check_value(
                    *node->exact_value, *ir.timescale(), policy, node->span);
                if (selected.ok())
                    record.after_checks.push_back(*selected.selected);
                else {
                    diagnostics.insert(diagnostics.end(),
                        selected.diagnostics.begin(), selected.diagnostics.end());
                    valid = false;
                }
            } else {
                const auto selected = select_sdf_delay(
                    *node->exact_value, *ir.timescale(), policy, node->span);
                if (selected.ok())
                    record.after_delays.push_back(*selected.selected);
                else {
                    diagnostics.insert(diagnostics.end(),
                        selected.diagnostics.begin(), selected.diagnostics.end());
                    valid = false;
                }
            }
        }
        return valid;
    }

    [[nodiscard]] std::string call_identity(const CallCandidate& candidate)
    {
        auto result = std::string { "sdf-vital-call-v1" };
        append_field(result, std::to_string(candidate.process->id));
        append_field(result, std::to_string(candidate.instruction));
        const auto& source = candidate.delay != nullptr
            ? candidate.delay->source_location
            : candidate.check->source;
        append_field(result, source.path);
        append_field(result, std::to_string(source.line));
        append_field(result, std::to_string(source.column));
        return result;
    }

    [[nodiscard]] std::string record_identity(
        const SdfVitalPathTimingRecord& record)
    {
        auto result = std::string { "sdf-vital-path-record-v1" };
        append_field(result, std::to_string(record.node_id));
        append_field(result, std::to_string(record.cell_id));
        append_field(result, record.instance_path);
        append_field(result,
            std::to_string(static_cast<unsigned>(record.annotation_mode)));
        append_field(result, record.call.canonical_identity);
        append_field(result, record.condition_identity);
        for (const auto& edge : record.edge_identities)
            append_field(result, edge);
        for (const auto value : record.before_delay_ticks)
            append_field(result, std::to_string(value));
        for (const auto& value : record.after_delays)
            append_field(result, value.canonical_identity);
        for (const auto value : record.before_check_ticks)
            append_field(result, std::to_string(value));
        for (const auto& value : record.after_checks)
            append_field(result, value.canonical_identity);
        append_field(result, record.source_identity);
        return result;
    }
} // namespace

SdfVitalPathTimingPlan::SdfVitalPathTimingPlan(
    std::shared_ptr<const SdfVitalTargetPlan> targets,
    SdfValuePolicy value_policy, std::vector<SdfVitalPathTimingRecord> records,
    std::string semantic_identity)
    : targets_(std::move(targets))
    , value_policy_(value_policy)
    , records_(std::move(records))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfVitalTargetPlan>&
SdfVitalPathTimingPlan::targets() const noexcept
{
    return targets_;
}

const SdfValuePolicy& SdfVitalPathTimingPlan::value_policy() const noexcept
{
    return value_policy_;
}

std::span<const SdfVitalPathTimingRecord> SdfVitalPathTimingPlan::records()
    const noexcept
{
    return records_;
}

std::string_view SdfVitalPathTimingPlan::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfVitalPathTimingResult::ok() const noexcept
{
    return plan != nullptr && diagnostics.empty();
}

SdfVitalPathTimingResult build_sdf_vital_path_timing_plan(
    std::shared_ptr<const SdfVitalTargetPlan> targets,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfValuePolicy& value_policy, const SdfVitalPathTimingLimits limits)
{
    SdfVitalPathTimingResult result;
    if (!targets || !targets->summary() || targets->semantic_identity().empty()
        || limits.max_records == 0U || limits.max_values_per_record == 0U
        || limits.max_identity_bytes == 0U
        || targets->targets().size() > limits.max_records) {
        diagnose(result.diagnostics,
            limits.max_records != 0U && targets
                    && targets->targets().size() > limits.max_records
                ? "FSIM-SDF-VITAL-PATH-006"
                : "FSIM-SDF-VITAL-PATH-001",
            "SDF VITAL path planning requires a complete target plan and nonzero limits",
            { });
        return result;
    }
    const auto& summary = *targets->summary();
    const auto& ir = *summary.endpoint_resolution()->cells()->scope()->normalized_ir();
    const auto state = elaborated.state();
    std::set<std::string> owners;
    std::vector<SdfVitalPathTimingRecord> records;
    records.reserve(targets->targets().size());
    std::size_t identity_bytes { };
    for (const auto& target : targets->targets()) {
        const auto* node = ir.find_node(target.node_id);
        const auto* specialization
            = specialization_for(state, target.instance_path);
        if (node == nullptr || specialization == nullptr
            || node->kind != target.construct_kind
            || (target.construct_kind != SdfConstructKind::Iopath
                && !timing_check_kind(target.construct_kind))) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PATH-001",
                "SDF VITAL target is stale or outside path and timing-check scope",
                node ? node->span : SourceSpan { });
            continue;
        }
        const auto calls = find_calls(state, *specialization, target);
        if (calls.size() != 1U) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PATH-002",
                "SDF VITAL target has no unique elaborated delay or timing-check call",
                node->span);
            continue;
        }
        const auto& candidate = calls.front();
        if (candidate.check != nullptr
            && !timing_kind_matches(
                target.construct_kind, candidate.check->kind)) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PATH-003",
                "SDF timing-check kind is incompatible with its VITAL call site",
                node->span);
            continue;
        }
        SdfVitalPathTimingRecord record;
        record.node_id = target.node_id;
        record.cell_id = target.cell_id;
        record.construct_kind = target.construct_kind;
        record.annotation_mode = annotation_mode(ir, target.node_id);
        record.instance_path = target.instance_path;
        record.call.process = candidate.process->id;
        record.call.instruction = candidate.instruction;
        record.call.kind = candidate.delay != nullptr
            ? SdfVitalCallKind::Delay
            : SdfVitalCallKind::TimingCheck;
        record.call.source = candidate.delay != nullptr
            ? candidate.delay->source_location
            : candidate.check->source;
        record.call.canonical_identity = call_identity(candidate);
        record.condition_identity = target.condition_identity;
        record.source = node->span;
        record.source_identity = node->source_identity;
        for (const auto& port : target.ports) {
            record.endpoint_signals.push_back(port.signal);
            if (!port.edge_identity.empty())
                record.edge_identities.push_back(port.edge_identity);
        }
        std::ranges::sort(record.endpoint_signals);
        record.endpoint_signals.erase(
            std::ranges::unique(record.endpoint_signals).begin(),
            record.endpoint_signals.end());
        std::ranges::sort(record.edge_identities);
        record.edge_identities.erase(
            std::ranges::unique(record.edge_identities).begin(),
            record.edge_identities.end());
        bool valid = before_values(candidate, record, result.diagnostics);
        valid = after_values(ir, target, value_policy, limits, record,
                    result.diagnostics)
            && valid;
        if (!valid)
            continue;
        const auto required = candidate.delay != nullptr
            ? delay_count(candidate.delay->shape)
            : candidate.check->limits.size();
        const auto proposed = candidate.delay != nullptr
            ? record.after_delays.size()
            : record.after_checks.size();
        if (proposed == 0U || proposed > required) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PATH-004",
                "SDF VITAL proposed path/check values have incompatible arity",
                node->span);
            continue;
        }
        if (!owners.insert(record.call.canonical_identity).second) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PATH-006",
                "SDF VITAL plan duplicates one stable call-site owner",
                node->span);
            continue;
        }
        record.canonical_identity = record_identity(record);
        if (record.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - record.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PATH-006",
                "SDF VITAL path plan exceeds its identity-byte limit",
                node->span);
            continue;
        }
        identity_bytes += record.canonical_identity.size();
        records.push_back(std::move(record));
    }
    if (!result.diagnostics.empty()
        || records.size() != targets->targets().size()) {
        return result;
    }
    auto semantic_identity = std::string { "sdf-vital-path-plan-v1" };
    append_field(semantic_identity, targets->semantic_identity());
    for (const auto& record : records)
        append_field(semantic_identity, record.canonical_identity);
    if (semantic_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-PATH-006",
            "SDF VITAL path semantic identity exceeds its limit", { });
        return result;
    }
    result.plan = std::make_shared<const SdfVitalPathTimingPlan>(
        std::move(targets), value_policy, std::move(records),
        std::move(semantic_identity));
    return result;
}

} // namespace fsim::app
