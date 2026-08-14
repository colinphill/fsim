// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_vital_reannotation.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::app {
namespace {
    using runtime::PackedLogic4;
    using runtime::SimulationTick;
    using runtime::simir::Extract;
    using runtime::simir::LoadConstant;
    using runtime::simir::operation_get_if;
    using runtime::simir::Process;
    using runtime::simir::RegisterId;
    using runtime::simir::VitalDelay;
    using runtime::simir::VitalTimingCheck;

    void diagnose(std::vector<frontend::Diagnostic>& diagnostics,
        std::string code, std::string message)
    {
        frontend::Diagnostic diagnostic;
        diagnostic.severity = frontend::DiagnosticSeverity::Error;
        diagnostic.code = std::move(code);
        diagnostic.message = std::move(message);
        diagnostics.push_back(std::move(diagnostic));
    }

    void append_field(std::string& output, const std::string_view value)
    {
        output += '|';
        output += std::to_string(value.size());
        output += ':';
        output += value;
    }

    [[nodiscard]] bool glob_matches(
        const std::string_view pattern, const std::string_view value) noexcept
    {
        std::size_t pattern_index { };
        std::size_t value_index { };
        std::size_t star = std::string_view::npos;
        std::size_t retry { };
        while (value_index < value.size()) {
            if (pattern_index < pattern.size()
                && (pattern[pattern_index] == '?'
                    || pattern[pattern_index] == value[value_index])) {
                ++pattern_index;
                ++value_index;
            } else if (pattern_index < pattern.size()
                && pattern[pattern_index] == '*') {
                star = pattern_index++;
                retry = value_index;
            } else if (star != std::string_view::npos) {
                pattern_index = star + 1U;
                value_index = ++retry;
            } else {
                return false;
            }
        }
        while (pattern_index < pattern.size()
            && pattern[pattern_index] == '*') {
            ++pattern_index;
        }
        return pattern_index == pattern.size();
    }

    [[nodiscard]] bool instance_in_root(const std::string_view instance,
        const std::string_view root) noexcept
    {
        return instance == root
            || (instance.size() > root.size()
                && instance.starts_with(root)
                && instance[root.size()] == '.');
    }

    [[nodiscard]] bool scope_matches(const std::string_view instance,
        const SdfVitalReannotationLayer& layer) noexcept
    {
        if (!instance_in_root(instance, layer.root))
            return false;
        const auto relative = instance == layer.root
            ? std::string_view { }
            : instance.substr(layer.root.size() + 1U);
        return glob_matches(layer.cell_pattern, instance)
            || glob_matches(layer.cell_pattern, relative);
    }

    [[nodiscard]] auto precedence_key(
        const SdfVitalReannotationLayer& layer)
    {
        return std::tie(layer.file_precedence, layer.cell_precedence);
    }

    [[nodiscard]] std::map<std::string, std::string> call_instances(
        const SdfVitalTimingCheckApplication& application)
    {
        std::map<std::string, std::string> result;
        const auto& precedence = application.scheduling()->precedence();
        const auto& paths = precedence->source()->paths();
        if (!paths)
            return result;
        for (const auto& path : paths->records())
            result.emplace(path.call.canonical_identity, path.instance_path);
        return result;
    }

    [[nodiscard]] bool same_delay_topology(const SdfVitalScheduledDelay& left,
        const SdfVitalScheduledDelay& right)
    {
        const auto expected
            = left.shape == runtime::simir::VitalDelayShape::single
            ? 1U
            : left.shape == runtime::simir::VitalDelayShape::delay01 ? 2U
                                                                     : 6U;
        return left.call == right.call && left.kind == right.kind
            && left.shape == right.shape && left.mode == right.mode
            && left.endpoint_signals == right.endpoint_signals
            && left.effective_delay_ticks.size() == expected
            && left.effective_delay_ticks.size()
            == right.effective_delay_ticks.size()
            && left.reject_fast_path == right.reject_fast_path
            && left.negative_preemption == right.negative_preemption;
    }

    [[nodiscard]] bool same_check_topology(
        const SdfVitalScheduledTimingCheck& left,
        const SdfVitalScheduledTimingCheck& right)
    {
        return left.call == right.call && left.kind == right.kind
            && left.endpoint_signals == right.endpoint_signals
            && left.edge_identities == right.edge_identities
            && left.condition_identity == right.condition_identity
            && left.check_enabled == right.check_enabled
            && left.enables == right.enables && left.x_on == right.x_on
            && left.message_on == right.message_on
            && left.severity == right.severity && left.message == right.message
            && left.violation_source == right.violation_source;
    }

    template <typename Target>
    [[nodiscard]] std::map<std::string, std::size_t> target_indices(
        const std::span<const Target> targets)
    {
        std::map<std::string, std::size_t> result;
        for (std::size_t index = 0; index < targets.size(); ++index) {
            if (!result.emplace(
                           targets[index].call.canonical_identity, index)
                    .second) {
                return { };
            }
        }
        return result;
    }

    [[nodiscard]] bool valid_topology(
        const SdfVitalTimingCheckApplication& baseline,
        const SdfVitalTimingCheckApplication& layer)
    {
        const auto baseline_delays = baseline.scheduling()->delays();
        const auto layer_delays = layer.scheduling()->delays();
        const auto baseline_checks = baseline.checks();
        const auto layer_checks = layer.checks();
        if (baseline_delays.size() != layer_delays.size()
            || baseline_checks.size() != layer_checks.size()) {
            return false;
        }
        const auto delay_indices = target_indices(layer_delays);
        const auto check_indices = target_indices(layer_checks);
        if (delay_indices.size() != layer_delays.size()
            || check_indices.size() != layer_checks.size()) {
            return false;
        }
        for (const auto& target : baseline_delays) {
            const auto found
                = delay_indices.find(target.call.canonical_identity);
            if (found == delay_indices.end()
                || !same_delay_topology(target, layer_delays[found->second])) {
                return false;
            }
        }
        for (const auto& target : baseline_checks) {
            const auto found
                = check_indices.find(target.call.canonical_identity);
            if (found == check_indices.end()
                || !same_check_topology(target, layer_checks[found->second])) {
                return false;
            }
        }
        return true;
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

    void rewrite_delay(Process& process, const std::size_t instruction,
        const std::span<const std::uint64_t> values)
    {
        if (instruction >= process.operations.size())
            throw std::invalid_argument { "stale VITAL delay instruction" };
        const auto* delay
            = operation_get_if<VitalDelay>(&process.operations[instruction]);
        if (delay == nullptr || delay_count(delay->shape) != values.size())
            throw std::invalid_argument { "changed VITAL delay topology" };
        std::map<RegisterId, std::size_t> definitions;
        for (std::size_t index = 0; index < instruction; ++index) {
            if (const auto* load
                = operation_get_if<LoadConstant>(&process.operations[index])) {
                definitions[load->destination] = index;
            } else if (const auto* extract
                = operation_get_if<Extract>(&process.operations[index])) {
                definitions[extract->destination] = index;
            }
        }
        std::map<std::size_t, std::pair<RegisterId, SimulationTick>> planned;
        const auto plan = [&](const RegisterId target,
                              const SimulationTick value) {
            const auto found = definitions.find(target);
            if (found == definitions.end())
                throw std::invalid_argument { "missing VITAL delay definition" };
            const auto [position, inserted]
                = planned.emplace(found->second, std::pair { target, value });
            if (!inserted && position->second != std::pair { target, value }) {
                throw std::invalid_argument { "conflicting VITAL delay values" };
            }
        };
        for (std::size_t index = 0; index < values.size(); ++index) {
            plan(delay->default_delays[index], values[index]);
            for (const auto& path : delay->paths)
                plan(path.delays[index], values[index]);
        }
        for (const auto& [index, replacement] : planned) {
            process.operations[index] = LoadConstant { replacement.first,
                PackedLogic4::from_aval_bval(
                    64U, replacement.second, 0U) };
        }
    }

    struct Winner {
        std::size_t layer { };
        std::size_t source { };
        bool timing_check { };
    };

    [[nodiscard]] std::vector<std::string> generic_identities(
        const SdfVitalTimingCheckApplication& application,
        const std::string_view call_identity)
    {
        std::vector<std::string> result;
        for (const auto& generic : application.scheduling()
                 ->precedence()
                 ->generics()) {
            if (generic.call_identity == call_identity)
                result.push_back(generic.generic_identity);
        }
        std::ranges::sort(result);
        return result;
    }
} // namespace

SdfVitalReannotationApplication::SdfVitalReannotationApplication(
    std::shared_ptr<const SdfVitalTimingCheckApplication> baseline,
    elaboration::ElaboratedDesign design,
    std::vector<SdfVitalScheduledDelay> delays,
    std::vector<SdfVitalScheduledTimingCheck> checks,
    std::vector<SdfVitalTimingGenericValue> generics,
    std::vector<runtime::simir::Interpreter::VitalTimingReannotation> updates,
    std::vector<SdfVitalReannotationRevision> revisions,
    const std::uint64_t generation,
    const SdfVitalPendingTransactionPolicy pending_policy,
    const SdfVitalTimingStatePolicy timing_state_policy,
    std::string semantic_identity)
    : baseline_(std::move(baseline))
    , design_(std::move(design))
    , delays_(std::move(delays))
    , checks_(std::move(checks))
    , generics_(std::move(generics))
    , updates_(std::move(updates))
    , revisions_(std::move(revisions))
    , generation_(generation)
    , pending_policy_(pending_policy)
    , timing_state_policy_(timing_state_policy)
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfVitalTimingCheckApplication>&
SdfVitalReannotationApplication::baseline() const noexcept
{
    return baseline_;
}

const elaboration::ElaboratedDesign&
SdfVitalReannotationApplication::design() const noexcept
{
    return design_;
}

std::span<const SdfVitalScheduledDelay>
SdfVitalReannotationApplication::delays() const noexcept
{
    return delays_;
}

std::span<const SdfVitalScheduledTimingCheck>
SdfVitalReannotationApplication::checks() const noexcept
{
    return checks_;
}

std::span<const SdfVitalTimingGenericValue>
SdfVitalReannotationApplication::generics() const noexcept
{
    return generics_;
}

std::span<const runtime::simir::Interpreter::VitalTimingReannotation>
SdfVitalReannotationApplication::updates() const noexcept
{
    return updates_;
}

std::span<const SdfVitalReannotationRevision>
SdfVitalReannotationApplication::revisions() const noexcept
{
    return revisions_;
}

std::uint64_t SdfVitalReannotationApplication::generation() const noexcept
{
    return generation_;
}

SdfVitalPendingTransactionPolicy
SdfVitalReannotationApplication::pending_policy() const noexcept
{
    return pending_policy_;
}

SdfVitalTimingStatePolicy
SdfVitalReannotationApplication::timing_state_policy() const noexcept
{
    return timing_state_policy_;
}

std::string_view
SdfVitalReannotationApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfVitalReannotationResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

bool SdfVitalReannotationCommitResult::ok() const noexcept
{
    return committed && diagnostics.empty();
}

SdfVitalReannotationResult apply_sdf_vital_reannotation(
    std::shared_ptr<const SdfVitalTimingCheckApplication> baseline,
    const std::span<const SdfVitalReannotationLayer> layers,
    const std::uint64_t generation,
    const SdfVitalPendingTransactionPolicy pending_policy,
    const SdfVitalTimingStatePolicy timing_state_policy,
    const SdfVitalReannotationLimits limits)
{
    SdfVitalReannotationResult result;
    if (!baseline || !baseline->scheduling()
        || baseline->semantic_identity().empty() || layers.empty()
        || generation == 0U || limits.max_files == 0U
        || limits.max_targets == 0U || limits.max_pattern_bytes == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-001",
            "SDF VITAL reannotation requires a baseline, layers, generation and nonzero limits");
        return result;
    }
    if (layers.size() > limits.max_files) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-004",
            "SDF VITAL reannotation exceeds its configured file limit");
        return result;
    }
    const auto instances = call_instances(*baseline);
    const std::set<std::string_view> roots(
        baseline->design().roots().begin(), baseline->design().roots().end());
    std::vector<std::map<std::string, std::size_t>> layer_delays;
    std::vector<std::map<std::string, std::size_t>> layer_checks;
    layer_delays.reserve(layers.size());
    layer_checks.reserve(layers.size());
    std::size_t pattern_bytes { };
    for (const auto& layer : layers) {
        const auto added = layer.file_identity.size() + layer.root.size()
            + layer.cell_pattern.size();
        if (!layer.timing || !layer.timing->scheduling()
            || layer.timing->semantic_identity().empty()
            || layer.file_identity.empty() || layer.root.empty()
            || layer.cell_pattern.empty() || !roots.contains(layer.root)
            || added > limits.max_pattern_bytes
            || pattern_bytes > limits.max_pattern_bytes - added) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-001",
                "SDF VITAL reannotation contains an incomplete or oversized scope");
            return result;
        }
        pattern_bytes += added;
        if (!valid_topology(*baseline, *layer.timing)) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-003",
                "SDF VITAL reannotation layer changes call topology");
            return result;
        }
        layer_delays.push_back(
            target_indices(layer.timing->scheduling()->delays()));
        layer_checks.push_back(target_indices(layer.timing->checks()));
    }

    std::map<std::string, Winner> winners;
    std::vector<bool> matched(layers.size());
    const auto select = [&](const std::string& identity,
                            const std::size_t source,
                            const bool timing_check,
                            const std::size_t layer_index) {
        const auto instance = instances.find(identity);
        if (instance == instances.end()
            || !scope_matches(instance->second, layers[layer_index])) {
            return true;
        }
        matched[layer_index] = true;
        const Winner candidate { layer_index, source, timing_check };
        const auto [position, inserted] = winners.emplace(identity, candidate);
        if (inserted)
            return true;
        auto& winner = position->second;
        const auto& old_layer = layers[winner.layer];
        const auto& new_layer = layers[layer_index];
        if (precedence_key(old_layer) == precedence_key(new_layer)) {
            const bool duplicate = timing_check
                ? old_layer.timing->checks()[winner.source].effective_limits
                    == new_layer.timing->checks()[source].effective_limits
                : old_layer.timing->scheduling()->delays()[winner.source].effective_delay_ticks
                    == new_layer.timing->scheduling()->delays()[source].effective_delay_ticks;
            diagnose(result.diagnostics,
                duplicate ? "FSIM-SDF-VITAL-REANNOTATION-002"
                          : "FSIM-SDF-VITAL-REANNOTATION-003",
                duplicate
                    ? "SDF VITAL reannotation contains an exact duplicate target at equal precedence"
                    : "SDF VITAL reannotation contains conflicting target values at equal precedence");
            return false;
        }
        if (precedence_key(old_layer) < precedence_key(new_layer))
            winner = candidate;
        return true;
    };
    for (std::size_t layer_index = 0; layer_index < layers.size();
        ++layer_index) {
        for (const auto& [identity, source] : layer_delays[layer_index]) {
            if (!select(identity, source, false, layer_index))
                return result;
        }
        for (const auto& [identity, source] : layer_checks[layer_index]) {
            if (!select(identity, source, true, layer_index))
                return result;
        }
    }
    if (std::ranges::find(matched, false) != matched.end()) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-001",
            "SDF VITAL reannotation cell scope matches no timing target");
        return result;
    }
    if (winners.size() > limits.max_targets) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-004",
            "SDF VITAL reannotation exceeds its configured target limit");
        return result;
    }

    auto state = baseline->design().state();
    auto delays = std::vector<SdfVitalScheduledDelay> {
        baseline->scheduling()->delays().begin(),
        baseline->scheduling()->delays().end()
    };
    auto checks = std::vector<SdfVitalScheduledTimingCheck> {
        baseline->checks().begin(), baseline->checks().end()
    };
    auto generics = std::vector<SdfVitalTimingGenericValue> {
        baseline->scheduling()->precedence()->generics().begin(),
        baseline->scheduling()->precedence()->generics().end()
    };
    const auto baseline_delay_indices
        = target_indices(baseline->scheduling()->delays());
    const auto baseline_check_indices = target_indices(baseline->checks());
    std::vector<runtime::simir::Interpreter::VitalTimingReannotation> updates;
    std::vector<SdfVitalReannotationRevision> revisions;
    updates.reserve(winners.size());
    revisions.reserve(winners.size());
    std::size_t identity_bytes { };
    try {
        for (const auto& [identity, winner] : winners) {
            const auto& layer = layers[winner.layer];
            SdfVitalReannotationRevision revision;
            revision.target_identity = identity;
            revision.file_identity = layer.file_identity;
            revision.root = layer.root;
            revision.cell_pattern = layer.cell_pattern;
            revision.file_precedence = layer.file_precedence;
            revision.cell_precedence = layer.cell_precedence;
            revision.timing_check = winner.timing_check;
            revision.generic_identities
                = generic_identities(*layer.timing, identity);
            runtime::simir::Interpreter::VitalTimingReannotation update;
            if (winner.timing_check) {
                const auto& source = layer.timing->checks()[winner.source];
                const auto destination = baseline_check_indices.at(identity);
                checks[destination] = source;
                update.process = source.call.process;
                update.instruction = source.call.instruction;
                update.timing_check = true;
                update.value_count = 4U;
                std::ranges::copy(source.effective_limits, update.values.begin());
                auto& process = state.processes.at(update.process);
                auto* operation = operation_get_if<VitalTimingCheck>(
                    &process.operations.at(update.instruction));
                if (operation == nullptr)
                    throw std::invalid_argument { "stale VITAL check" };
                operation->limits = source.effective_limits;
            } else {
                const auto& source
                    = layer.timing->scheduling()->delays()[winner.source];
                const auto destination = baseline_delay_indices.at(identity);
                delays[destination] = source;
                update.process = source.call.process;
                update.instruction = source.call.instruction;
                update.value_count = source.effective_delay_ticks.size();
                std::ranges::copy(
                    source.effective_delay_ticks, update.values.begin());
                rewrite_delay(state.processes.at(update.process),
                    update.instruction, source.effective_delay_ticks);
            }
            revision.canonical_identity
                = "sdf-vital-reannotation-revision-v1";
            append_field(revision.canonical_identity, identity);
            append_field(revision.canonical_identity, layer.file_identity);
            append_field(revision.canonical_identity, layer.root);
            append_field(revision.canonical_identity, layer.cell_pattern);
            append_field(revision.canonical_identity,
                std::to_string(layer.file_precedence));
            append_field(revision.canonical_identity,
                std::to_string(layer.cell_precedence));
            for (const auto& generic : revision.generic_identities)
                append_field(revision.canonical_identity, generic);
            const auto size = revision.canonical_identity.size();
            if (size > limits.max_identity_bytes
                || identity_bytes > limits.max_identity_bytes - size) {
                diagnose(result.diagnostics,
                    "FSIM-SDF-VITAL-REANNOTATION-004",
                    "SDF VITAL reannotation exceeds its identity-byte limit");
                return result;
            }
            identity_bytes += size;
            updates.push_back(update);
            revisions.push_back(std::move(revision));

            std::erase_if(generics, [&](const auto& generic) {
                return generic.call_identity == identity;
            });
            for (const auto& generic : layer.timing->scheduling()
                     ->precedence()
                     ->generics()) {
                if (generic.call_identity == identity)
                    generics.push_back(generic);
            }
        }
    } catch (const std::exception& error) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-003",
            std::string { "SDF VITAL reannotation cannot publish atomically: " }
                + error.what());
        return result;
    }
    std::ranges::sort(generics, { }, [](const auto& generic) {
        return std::tuple { generic.call_identity, generic.value_index,
            generic.generic_identity };
    });
    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    if (!design) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-003",
            "SDF VITAL reannotation produced invalid effective design state");
        return result;
    }
    std::string semantic_identity = "sdf-vital-reannotation-application-v1";
    append_field(semantic_identity, baseline->semantic_identity());
    append_field(semantic_identity, std::to_string(generation));
    append_field(semantic_identity,
        std::to_string(static_cast<unsigned>(pending_policy)));
    append_field(semantic_identity,
        std::to_string(static_cast<unsigned>(timing_state_policy)));
    for (const auto& revision : revisions)
        append_field(semantic_identity, revision.canonical_identity);
    if (semantic_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-004",
            "SDF VITAL reannotation semantic identity exceeds its limit");
        return result;
    }
    result.application
        = std::make_shared<const SdfVitalReannotationApplication>(
            std::move(baseline), std::move(*design), std::move(delays),
            std::move(checks), std::move(generics), std::move(updates),
            std::move(revisions), generation, pending_policy,
            timing_state_policy, std::move(semantic_identity));
    return result;
}

SdfVitalReannotationCommitResult commit_sdf_vital_reannotation(
    const SdfVitalReannotationApplication& application,
    runtime::simir::Interpreter& interpreter)
{
    SdfVitalReannotationCommitResult result;
    result.generation = application.generation();
    if (application.pending_policy()
            == SdfVitalPendingTransactionPolicy::RejectIfPending
        && interpreter.scheduler().has_pending()) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-001",
            "SDF VITAL reannotation rejects a scheduler with pending transactions");
        return result;
    }
    try {
        interpreter.reannotate_vital_timing(application.updates(),
            application.timing_state_policy()
                == SdfVitalTimingStatePolicy::ResetHistory);
        result.committed = true;
    } catch (const std::exception& error) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-REANNOTATION-001",
            std::string { "SDF VITAL reannotation commit rejected: " }
                + error.what());
    }
    return result;
}

} // namespace fsim::app
