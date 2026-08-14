// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_scheduling.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SourceSpan;
    using runtime::simir::LoadConstant;
    using runtime::simir::Process;
    using runtime::simir::RegisterId;
    using runtime::simir::VitalDelay;
    using runtime::simir::VitalDelayShape;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span = { })
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    void append_field(std::string& identity, const std::string_view field)
    {
        identity += '|';
        identity += std::to_string(field.size());
        identity += ':';
        identity += field;
    }

    [[nodiscard]] std::size_t delay_count(const VitalDelayShape shape)
    {
        switch (shape) {
        case VitalDelayShape::single:
            return 1U;
        case VitalDelayShape::delay01:
            return 2U;
        case VitalDelayShape::delay01z:
            return 6U;
        }
        return 0U;
    }

    [[nodiscard]] bool valid_mode(const runtime::simir::VitalGlitchMode mode)
    {
        using runtime::simir::VitalGlitchMode;
        switch (mode) {
        case VitalGlitchMode::on_detect:
        case VitalGlitchMode::on_event:
        case VitalGlitchMode::transport:
        case VitalGlitchMode::inertial:
            return true;
        }
        return false;
    }

    using ValueGroups = std::map<std::string_view,
        std::vector<const SdfVitalEffectiveTimingValue*>, std::less<>>;

    [[nodiscard]] ValueGroups group_delay_values(
        const SdfVitalPrecedenceApplication& precedence)
    {
        ValueGroups groups;
        for (const auto& value : precedence.values()) {
            if (!value.timing_check)
                groups[value.call_identity].push_back(&value);
        }
        for (auto& [identity, values] : groups) {
            (void)identity;
            std::ranges::sort(values, { },
                &SdfVitalEffectiveTimingValue::value_index);
        }
        return groups;
    }

    [[nodiscard]] bool collect_ticks(
        const SdfVitalPathTimingRecord& source,
        const std::vector<const SdfVitalEffectiveTimingValue*>& values,
        std::vector<std::uint64_t>& ticks,
        std::vector<Diagnostic>& diagnostics)
    {
        if (values.size() != source.before_delay_ticks.size()
            || values.empty()) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-SCHEDULING-003",
                "SDF VITAL scheduled delay has incomplete shape/value arity",
                source.source);
            return false;
        }
        ticks.reserve(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto* value = values[index];
            if (value->value_index != index || value->timing_check
                || value->call_identity != source.call.canonical_identity
                || value->source_delay_ticks != source.before_delay_ticks[index]
                || !value->effective_delay_ticks
                || (value->no_annotation
                    && value->effective_delay_ticks
                        != value->source_delay_ticks)) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-SCHEDULING-003",
                    "SDF VITAL scheduled delay is stale, negative, missing, or noncontiguous",
                    source.source);
                return false;
            }
            ticks.push_back(*value->effective_delay_ticks);
        }
        return true;
    }

    struct RetainedDefinition {
        std::size_t instruction { };
        runtime::PackedLogic4 value;
    };

    [[nodiscard]] runtime::PackedLogic4 extract_value(
        const runtime::PackedLogic4& source, const std::uint32_t offset,
        const std::uint32_t width)
    {
        runtime::PackedLogic4 result(width, runtime::Logic4::zero);
        for (std::size_t bit = 0; bit < width; ++bit)
            result.set(bit, source.get(offset + bit));
        return result;
    }

    [[nodiscard]] std::unordered_map<RegisterId, RetainedDefinition>
    retained_definitions(Process& process, const std::size_t instruction)
    {
        std::unordered_map<RegisterId, RetainedDefinition> definitions;
        for (std::size_t index = 0;
            index < instruction && index < process.operations.size(); ++index) {
            auto* load = runtime::simir::operation_get_if<LoadConstant>(
                &process.operations[index]);
            if (load != nullptr) {
                definitions[load->destination] = { index, load->value };
                continue;
            }
            const auto* extract = runtime::simir::operation_get_if<
                runtime::simir::Extract>(&process.operations[index]);
            if (extract == nullptr)
                continue;
            const auto source = definitions.find(extract->source);
            if (source == definitions.end()
                || static_cast<std::size_t>(extract->offset)
                        + extract->width
                    > source->second.value.width()) {
                definitions.erase(extract->destination);
                continue;
            }
            definitions[extract->destination] = { index,
                extract_value(source->second.value, extract->offset,
                    extract->width) };
        }
        return definitions;
    }

    [[nodiscard]] bool source_tick_matches(
        const std::unordered_map<RegisterId, RetainedDefinition>& definitions,
        const RegisterId id, const std::uint64_t expected)
    {
        const auto found = definitions.find(id);
        if (found == definitions.end())
            return false;
        const auto word = found->second.value.low_word();
        return word.bval == 0U && word.aval == expected;
    }

    [[nodiscard]] bool replace_tick(Process& process,
        const std::unordered_map<RegisterId, RetainedDefinition>& definitions,
        const RegisterId id, const std::uint64_t tick)
    {
        const auto found = definitions.find(id);
        if (found == definitions.end())
            return false;
        process.operations[found->second.instruction] = LoadConstant { id,
            runtime::PackedLogic4::from_aval_bval(64U, tick, 0U) };
        return true;
    }

    [[nodiscard]] bool apply_delay(Process& process,
        const SdfVitalPathTimingRecord& source,
        const std::vector<const SdfVitalEffectiveTimingValue*>& values,
        SdfVitalScheduledDelay& scheduled,
        std::vector<Diagnostic>& diagnostics)
    {
        if (source.call.instruction >= process.operations.size()) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-SCHEDULING-002",
                "SDF VITAL scheduled call instruction is outside its process",
                source.source);
            return false;
        }
        auto* operation = runtime::simir::operation_get_if<VitalDelay>(
            &process.operations[source.call.instruction]);
        if (operation == nullptr
            || delay_count(operation->shape) != source.before_delay_ticks.size()) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-SCHEDULING-002",
                "SDF VITAL scheduled call no longer owns its delay shape",
                source.source);
            return false;
        }
        if (!valid_mode(operation->mode)) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-SCHEDULING-004",
                "SDF VITAL scheduled call has an unsupported transition policy",
                source.source);
            return false;
        }
        std::vector<std::uint64_t> ticks;
        if (!collect_ticks(source, values, ticks, diagnostics))
            return false;
        auto definitions
            = retained_definitions(process, source.call.instruction);
        for (std::size_t index = 0; index < ticks.size(); ++index) {
            if (!source_tick_matches(definitions,
                    operation->default_delays[index],
                    source.before_delay_ticks[index])) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-SCHEDULING-002",
                    "SDF VITAL scheduled call has stale static source-delay definitions",
                    source.source);
                return false;
            }
        }
        for (std::size_t index = 0; index < ticks.size(); ++index) {
            if (!replace_tick(process, definitions,
                    operation->default_delays[index], ticks[index])) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-SCHEDULING-005",
                    "SDF VITAL delay publication lost a default-delay definition",
                    source.source);
                return false;
            }
            for (const auto& path : operation->paths) {
                if (!replace_tick(process, definitions, path.delays[index],
                        ticks[index])) {
                    diagnose(diagnostics, "FSIM-SDF-VITAL-SCHEDULING-005",
                        "SDF VITAL delay publication lost a path-delay definition",
                        source.source);
                    return false;
                }
            }
        }
        scheduled.call = source.call;
        scheduled.kind = operation->kind;
        scheduled.shape = operation->shape;
        scheduled.mode = operation->mode;
        scheduled.endpoint_signals = source.endpoint_signals;
        scheduled.source_delay_ticks = source.before_delay_ticks;
        scheduled.effective_delay_ticks = std::move(ticks);
        scheduled.annotated = std::ranges::any_of(values,
            [](const auto* value) { return !value->no_annotation; });
        scheduled.reject_fast_path = operation->reject_fast_path;
        scheduled.negative_preemption = operation->negative_preemption;
        auto identity = std::string { "sdf-vital-scheduled-delay-v1" };
        append_field(identity, source.call.canonical_identity);
        append_field(identity,
            std::to_string(static_cast<unsigned>(scheduled.kind)));
        append_field(identity,
            std::to_string(static_cast<unsigned>(scheduled.shape)));
        append_field(identity,
            std::to_string(static_cast<unsigned>(scheduled.mode)));
        append_field(identity, scheduled.annotated ? "annotated" : "source");
        for (const auto* value : values)
            append_field(identity, value->canonical_identity);
        scheduled.canonical_identity = std::move(identity);
        return true;
    }
} // namespace

const std::shared_ptr<const SdfVitalPrecedenceApplication>&
SdfVitalSchedulingApplication::precedence() const noexcept
{
    return precedence_;
}

const elaboration::ElaboratedDesign& SdfVitalSchedulingApplication::design()
    const noexcept
{
    return design_;
}

std::span<const SdfVitalScheduledDelay>
SdfVitalSchedulingApplication::delays() const noexcept
{
    return delays_;
}

std::string_view SdfVitalSchedulingApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

SdfVitalSchedulingApplication::SdfVitalSchedulingApplication(
    std::shared_ptr<const SdfVitalPrecedenceApplication> precedence,
    elaboration::ElaboratedDesign design,
    std::vector<SdfVitalScheduledDelay> delays,
    std::string semantic_identity)
    : precedence_(std::move(precedence))
    , design_(std::move(design))
    , delays_(std::move(delays))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfVitalSchedulingResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfVitalSchedulingResult apply_sdf_vital_scheduling(
    std::shared_ptr<const SdfVitalPrecedenceApplication> precedence,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfVitalSchedulingLimits limits)
{
    SdfVitalSchedulingResult result;
    if (!precedence || !precedence->source()
        || !precedence->source()->paths()
        || precedence->semantic_identity().empty()
        || limits.max_calls == 0U || limits.max_values == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-SCHEDULING-001",
            "SDF VITAL scheduling requires complete precedence and nonzero limits");
        return result;
    }
    const auto groups = group_delay_values(*precedence);
    if (groups.size() > limits.max_calls
        || precedence->values().size() > limits.max_values) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-SCHEDULING-006",
            "SDF VITAL scheduling exceeds its call or value resource limit");
        return result;
    }
    auto state = elaborated.state();
    std::unordered_map<runtime::simir::ProcessId, Process*> processes;
    for (auto& process : state.processes) {
        if (!processes.emplace(process.id, &process).second) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-SCHEDULING-002",
                "SDF VITAL scheduling found duplicate process ownership");
            return result;
        }
    }
    std::vector<SdfVitalScheduledDelay> scheduled;
    scheduled.reserve(groups.size());
    std::unordered_set<std::string_view> owners;
    std::size_t identity_bytes { };
    for (const auto& source : precedence->source()->paths()->records()) {
        if (source.call.kind != SdfVitalCallKind::Delay)
            continue;
        const auto values = groups.find(source.call.canonical_identity);
        const auto process = processes.find(source.call.process);
        if (source.call.canonical_identity.empty()
            || !owners.insert(source.call.canonical_identity).second
            || values == groups.end() || process == processes.end()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-SCHEDULING-002",
                "SDF VITAL scheduled call has stale or duplicate ownership",
                source.source);
            return result;
        }
        SdfVitalScheduledDelay delay;
        if (!apply_delay(*process->second, source, values->second, delay,
                result.diagnostics)) {
            return result;
        }
        if (delay.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - delay.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-SCHEDULING-006",
                "SDF VITAL scheduled-delay identities exceed their byte limit",
                source.source);
            return result;
        }
        identity_bytes += delay.canonical_identity.size();
        scheduled.push_back(std::move(delay));
    }
    if (scheduled.size() != groups.size()) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-SCHEDULING-002",
            "SDF VITAL precedence contains a delay without a source call owner");
        return result;
    }
    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    if (!design) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-SCHEDULING-005",
            "SDF VITAL scheduling produced an invalid runtime design");
        return result;
    }
    auto semantic_identity = std::string { "sdf-vital-scheduling-v1" };
    append_field(semantic_identity, precedence->semantic_identity());
    for (const auto& delay : scheduled)
        append_field(semantic_identity, delay.canonical_identity);
    if (semantic_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-SCHEDULING-006",
            "SDF VITAL scheduling semantic identity exceeds its byte limit");
        return result;
    }
    result.application
        = std::make_shared<const SdfVitalSchedulingApplication>(
            std::move(precedence), std::move(*design), std::move(scheduled),
            std::move(semantic_identity));
    return result;
}

} // namespace fsim::app
