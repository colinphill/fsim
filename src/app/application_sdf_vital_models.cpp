// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_models.hpp"

#include <algorithm>
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

    void append_number(std::string& target, const std::uint64_t value)
    {
        append_field(target, std::to_string(value));
    }

    struct ModelFingerprint {
        bool valid { true };
        bool memory { };
        bool conditional { };
        bool case_equal { };
        bool last_value { };
        bool copy_register { };
        bool insert { };
        std::size_t memory_operations { };
        std::size_t conditional_operations { };
        std::vector<runtime::simir::ProcessId> processes;
        std::unordered_set<runtime::simir::ProcessId> process_set;

        [[nodiscard]] bool state_table() const noexcept
        {
            return conditional && case_equal
                && (last_value || (copy_register && insert));
        }
    };

    [[nodiscard]] ModelFingerprint fingerprint(
        const elaboration::ElaboratedDesignState& state,
        const elaboration::SpecializationInfo& specialization)
    {
        ModelFingerprint result;
        result.processes = specialization.processes;
        std::ranges::sort(result.processes);
        if (std::ranges::adjacent_find(result.processes)
            != result.processes.end()) {
            result.valid = false;
            return result;
        }
        result.process_set.insert(
            result.processes.begin(), result.processes.end());
        for (const auto process_id : result.processes) {
            if (process_id >= state.processes.size()
                || state.processes[process_id].id != process_id) {
                result.valid = false;
                continue;
            }
            for (const auto& operation : state.processes[process_id].operations) {
                if (runtime::simir::operation_get_if<
                        runtime::simir::VitalMemoryDeclare>(&operation)
                    != nullptr) {
                    result.memory = true;
                    ++result.memory_operations;
                }
                if (runtime::simir::operation_get_if<
                        runtime::simir::ConditionalSelect>(&operation)
                    != nullptr) {
                    result.conditional = true;
                    ++result.conditional_operations;
                }
                if (runtime::simir::operation_get_if<
                        runtime::simir::SignalLastValue>(&operation)
                    != nullptr) {
                    result.last_value = true;
                }
                if (runtime::simir::operation_get_if<
                        runtime::simir::CopyRegister>(&operation)
                    != nullptr) {
                    result.copy_register = true;
                }
                if (runtime::simir::operation_get_if<
                        runtime::simir::Insert>(&operation)
                    != nullptr) {
                    result.insert = true;
                }
                const auto* binary = runtime::simir::operation_get_if<
                    runtime::simir::Binary>(&operation);
                if (binary != nullptr
                    && binary->operation
                        == runtime::simir::BinaryOperator::case_equal) {
                    result.case_equal = true;
                }
            }
        }
        return result;
    }

    [[nodiscard]] bool wrapper_matches(
        const SdfVitalWrapperRegistration& wrapper,
        const SdfVitalPlannedTarget& target,
        const SdfVitalModelLimits& limits)
    {
        if (wrapper.target_identity != target.canonical_identity
            || wrapper.governance_identity.empty() || wrapper.ports.empty()
            || wrapper.ports.size() != target.ports.size()
            || wrapper.ports.size() > limits.max_wrapper_ports) {
            return false;
        }
        std::unordered_map<std::string_view,
            const SdfVitalWrapperPortProfile*>
            ports;
        for (const auto& port : wrapper.ports) {
            if (port.port_identity.empty()
                || !ports.emplace(port.port_identity, &port).second) {
                return false;
            }
        }
        for (const auto& expected : target.ports) {
            const auto found = ports.find(expected.canonical_identity);
            if (found == ports.end())
                return false;
            const auto& actual = *found->second;
            if (actual.signal != expected.signal || actual.role != expected.role
                || actual.width != expected.width
                || actual.direction != expected.direction
                || actual.value_domain != expected.value_domain) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string record_identity(
        const SdfVitalModelRecord& record,
        const ModelFingerprint& fingerprint)
    {
        auto result = std::string { "sdf-vital-model-record-v1" };
        append_number(result, record.node_id);
        append_number(result, record.cell_id);
        append_field(result, record.instance_path);
        append_number(result,
            static_cast<std::uint64_t>(record.structural_kind));
        append_number(result, record.governed_wrapper ? 1U : 0U);
        append_field(result, record.governance_identity);
        append_field(result, record.target_identity);
        append_field(result, record.path_identity);
        append_number(result, fingerprint.memory_operations);
        append_number(result, fingerprint.conditional_operations);
        for (const auto process : record.owned_processes)
            append_number(result, process);
        return result;
    }
} // namespace

SdfVitalModelPlan::SdfVitalModelPlan(
    std::shared_ptr<const SdfVitalPathTimingPlan> paths,
    std::vector<SdfVitalWrapperRegistration> wrappers,
    std::vector<SdfVitalModelRecord> records, std::string semantic_identity)
    : paths_(std::move(paths))
    , wrappers_(std::move(wrappers))
    , records_(std::move(records))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfVitalPathTimingPlan>&
SdfVitalModelPlan::paths() const noexcept
{
    return paths_;
}

std::span<const SdfVitalWrapperRegistration>
SdfVitalModelPlan::wrappers() const noexcept
{
    return wrappers_;
}

std::span<const SdfVitalModelRecord> SdfVitalModelPlan::records() const noexcept
{
    return records_;
}

std::string_view SdfVitalModelPlan::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfVitalModelResult::ok() const noexcept
{
    return plan != nullptr && diagnostics.empty();
}

SdfVitalModelResult build_sdf_vital_model_plan(
    std::shared_ptr<const SdfVitalPathTimingPlan> paths,
    const elaboration::ElaboratedDesign& elaborated,
    const std::span<const SdfVitalWrapperRegistration> wrappers,
    const SdfVitalModelLimits limits)
{
    SdfVitalModelResult result;
    if (!paths || !paths->targets() || paths->semantic_identity().empty()
        || paths->targets()->semantic_identity().empty()
        || paths->records().size() != paths->targets()->targets().size()
        || limits.max_records == 0U
        || limits.max_processes_per_model == 0U
        || limits.max_wrapper_ports == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-001",
            "SDF VITAL model planning requires a complete path plan and nonzero limits",
            { });
        return result;
    }
    if (paths->records().size() > limits.max_records
        || wrappers.size() > limits.max_records) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-006",
            "SDF VITAL model planning exceeds its record limit", { });
        return result;
    }

    std::unordered_map<std::uint64_t, const SdfVitalPlannedTarget*> targets;
    for (const auto& target : paths->targets()->targets()) {
        if (!targets.emplace(target.node_id, &target).second) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-005",
                "SDF VITAL model planning found duplicate target ownership",
                target.source);
        }
    }
    std::unordered_map<std::string_view,
        const elaboration::SpecializationInfo*>
        specializations;
    const auto& state = elaborated.state();
    for (const auto& specialization : state.specializations) {
        if (specialization.language != frontend::Language::Vhdl2008)
            continue;
        const auto [found, inserted] = specializations.emplace(
            specialization.instance, &specialization);
        if (!inserted)
            found->second = nullptr;
    }
    std::unordered_map<std::string_view,
        const SdfVitalWrapperRegistration*>
        wrapper_by_target;
    for (const auto& wrapper : wrappers) {
        if (wrapper.target_identity.empty()
            || wrapper.governance_identity.empty() || wrapper.ports.empty()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-004",
                "SDF VITAL wrapper registration is incomplete", { });
            continue;
        }
        if (wrapper.ports.size() > limits.max_wrapper_ports) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-006",
                "SDF VITAL wrapper registration exceeds its port limit", { });
            continue;
        }
        if (!wrapper_by_target.emplace(wrapper.target_identity, &wrapper).second) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-005",
                "SDF VITAL wrapper registrations duplicate target ownership",
                { });
        }
    }
    if (!result.diagnostics.empty())
        return result;

    std::unordered_map<std::string_view, ModelFingerprint> fingerprints;
    std::unordered_set<std::string_view> used_wrappers;
    std::set<std::string> owners;
    std::vector<SdfVitalModelRecord> records;
    records.reserve(paths->records().size());
    std::size_t identity_bytes { };
    for (const auto& path : paths->records()) {
        const auto target_entry = targets.find(path.node_id);
        const auto specialization_entry
            = specializations.find(path.instance_path);
        if (target_entry == targets.end()
            || specialization_entry == specializations.end()
            || specialization_entry->second == nullptr
            || target_entry->second->cell_id != path.cell_id
            || target_entry->second->canonical_identity.empty()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-001",
                "SDF VITAL model target or specialization identity is stale",
                path.source);
            continue;
        }
        const auto& target = *target_entry->second;
        const auto& specialization = *specialization_entry->second;
        auto [fingerprint_entry, inserted] = fingerprints.try_emplace(
            specialization.instance);
        if (inserted) {
            fingerprint_entry->second = fingerprint(state, specialization);
        }
        const auto& model = fingerprint_entry->second;
        if (!model.valid || model.processes.empty()
            || model.processes.size() > limits.max_processes_per_model
            || !model.process_set.contains(path.call.process)) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-002",
                "SDF VITAL model has missing, invalid, or excessive process ownership",
                path.source);
            continue;
        }
        if (model.memory && model.state_table()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-003",
                "SDF VITAL model combines incompatible memory and state-table shapes",
                path.source);
            continue;
        }

        SdfVitalModelRecord record;
        record.node_id = path.node_id;
        record.cell_id = path.cell_id;
        record.instance_path = path.instance_path;
        record.structural_kind = model.memory
            ? SdfVitalStructuralModelKind::MemoryPath
            : model.state_table() ? SdfVitalStructuralModelKind::StateTable
                                  : SdfVitalStructuralModelKind::Primitive;
        record.owned_processes = model.processes;
        record.call = path.call;
        record.target_identity = target.canonical_identity;
        record.path_identity = path.canonical_identity;
        record.source = path.source;
        const auto wrapper = wrapper_by_target.find(target.canonical_identity);
        if (wrapper != wrapper_by_target.end()) {
            if (!wrapper_matches(*wrapper->second, target, limits)) {
                diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-004",
                    "SDF VITAL wrapper registration does not match the target port structure",
                    path.source);
                continue;
            }
            record.governed_wrapper = true;
            record.governance_identity = wrapper->second->governance_identity;
            used_wrappers.insert(wrapper->first);
        }
        const auto owner = record.target_identity + '|'
            + record.call.canonical_identity;
        if (!owners.insert(owner).second) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-005",
                "SDF VITAL model records duplicate target and call ownership",
                path.source);
            continue;
        }
        record.canonical_identity = record_identity(record, model);
        if (record.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - record.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-006",
                "SDF VITAL model identities exceed their byte limit",
                path.source);
            continue;
        }
        identity_bytes += record.canonical_identity.size();
        records.push_back(std::move(record));
    }
    if (used_wrappers.size() != wrapper_by_target.size()) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-004",
            "SDF VITAL wrapper registration has no exact planned target", { });
    }
    if (!result.diagnostics.empty()
        || records.size() != paths->records().size()) {
        return result;
    }

    std::vector<SdfVitalWrapperRegistration> retained_wrappers(
        wrappers.begin(), wrappers.end());
    std::ranges::sort(retained_wrappers, { },
        &SdfVitalWrapperRegistration::target_identity);
    auto semantic_identity = std::string { "sdf-vital-model-plan-v1" };
    append_field(semantic_identity, paths->semantic_identity());
    for (const auto& wrapper : retained_wrappers) {
        append_field(semantic_identity, wrapper.target_identity);
        append_field(semantic_identity, wrapper.governance_identity);
    }
    for (const auto& record : records)
        append_field(semantic_identity, record.canonical_identity);
    if (semantic_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-MODEL-006",
            "SDF VITAL model semantic identity exceeds its byte limit", { });
        return result;
    }
    result.plan = std::make_shared<const SdfVitalModelPlan>(std::move(paths),
        std::move(retained_wrappers), std::move(records),
        std::move(semantic_identity));
    return result;
}

} // namespace fsim::app
