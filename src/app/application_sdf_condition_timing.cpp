// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_condition_timing.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SdfConstructKind;
    using frontend::SourceSpan;
    using runtime::simir::ModulePathExpression;
    using runtime::simir::ModulePathExpressionOperator;
    using runtime::simir::ModuleTimingCheck;
    using runtime::simir::ModuleTimingCheckKind;
    using runtime::simir::SignalId;

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

    [[nodiscard]] bool kind_matches(const SdfConstructKind construct,
        const ModuleTimingCheckKind kind) noexcept
    {
        switch (construct) {
        case SdfConstructKind::Setup:
            return kind == ModuleTimingCheckKind::setup;
        case SdfConstructKind::Hold:
            return kind == ModuleTimingCheckKind::hold;
        case SdfConstructKind::SetupHold:
            return kind == ModuleTimingCheckKind::setuphold;
        case SdfConstructKind::Recovery:
            return kind == ModuleTimingCheckKind::recovery;
        case SdfConstructKind::Removal:
            return kind == ModuleTimingCheckKind::removal;
        case SdfConstructKind::RecRem:
            return kind == ModuleTimingCheckKind::recrem;
        case SdfConstructKind::Skew:
            return kind == ModuleTimingCheckKind::skew
                || kind == ModuleTimingCheckKind::timeskew;
        case SdfConstructKind::BidirectSkew:
            return kind == ModuleTimingCheckKind::fullskew;
        case SdfConstructKind::Width:
            return kind == ModuleTimingCheckKind::width;
        case SdfConstructKind::Period:
            return kind == ModuleTimingCheckKind::period;
        case SdfConstructKind::NoChange:
            return kind == ModuleTimingCheckKind::nochange;
        default:
            return false;
        }
    }

    [[nodiscard]] const ModuleTimingCheck* find_check(
        const elaboration::ElaboratedDesign& design,
        const std::string_view identity)
    {
        const auto& checks = design.verilog_timing_checks();
        const auto found
            = std::ranges::find(checks, identity, &ModuleTimingCheck::identity);
        return found == checks.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool event_roles_match(
        const SdfPlannedAnnotation& annotation,
        const ModuleTimingCheck& check)
    {
        const bool reference = std::ranges::any_of(annotation.endpoints,
            [&](const auto& endpoint) {
                return endpoint.role == SdfEndpointRole::TimingReference
                    && endpoint.signal == check.reference.terminal.signal;
            });
        const bool data_endpoint = std::ranges::any_of(annotation.endpoints,
            [](const auto& endpoint) {
                return endpoint.role == SdfEndpointRole::TimingData;
            });
        if (!check.data)
            return reference && !data_endpoint;
        return reference && std::ranges::any_of(annotation.endpoints, [&](const auto& endpoint) {
            return endpoint.role == SdfEndpointRole::TimingData
                && endpoint.signal == check.data->terminal.signal;
        });
    }

    [[nodiscard]] bool event_is_controlled(
        const runtime::simir::ModuleTimingEvent& event) noexcept
    {
        return event.edge != runtime::simir::ModulePathEdge::none
            || !event.edge_descriptors.empty();
    }

    [[nodiscard]] bool edge_binding_matches(
        const SdfPlannedAnnotation& annotation,
        const ModuleTimingCheck& check)
    {
        std::vector<std::string> endpoint_edges;
        for (const auto& endpoint : annotation.endpoints) {
            if (endpoint.edge_identity.empty())
                continue;
            endpoint_edges.push_back(endpoint.edge_identity);
            if (endpoint.role == SdfEndpointRole::TimingReference) {
                if (endpoint.signal != check.reference.terminal.signal
                    || !event_is_controlled(check.reference)) {
                    return false;
                }
            } else if (endpoint.role == SdfEndpointRole::TimingData) {
                if (!check.data
                    || endpoint.signal != check.data->terminal.signal
                    || !event_is_controlled(*check.data)) {
                    return false;
                }
            } else {
                return false;
            }
        }
        std::ranges::sort(endpoint_edges);
        endpoint_edges.erase(
            std::ranges::unique(endpoint_edges).begin(), endpoint_edges.end());
        return endpoint_edges == annotation.edge_identities;
    }

    [[nodiscard]] bool valid_limits(const ModuleTimingCheckKind kind,
        const std::span<const std::int64_t> values) noexcept
    {
        const bool compound = kind == ModuleTimingCheckKind::setuphold
            || kind == ModuleTimingCheckKind::recrem
            || kind == ModuleTimingCheckKind::fullskew
            || kind == ModuleTimingCheckKind::nochange;
        if (values.size() != (compound ? 2U : 1U))
            return false;
        if (kind == ModuleTimingCheckKind::nochange)
            return values[0] <= values[1];
        if (kind == ModuleTimingCheckKind::setuphold
            || kind == ModuleTimingCheckKind::recrem) {
            const auto left = values[0];
            const auto right = values[1];
            if ((right > 0
                    && left > std::numeric_limits<std::int64_t>::max() - right)
                || (right < 0
                    && left
                        < std::numeric_limits<std::int64_t>::min() - right)) {
                return false;
            }
            return left + right > 0;
        }
        return std::ranges::none_of(
            values, [](const auto value) { return value < 0; });
    }

    void append_expression_identity(std::string& identity,
        const ModulePathExpression& expression)
    {
        append_field(identity, std::to_string(expression.root));
        append_field(identity, std::to_string(expression.nodes.size()));
        for (const auto& node : expression.nodes) {
            append_field(identity,
                std::to_string(static_cast<unsigned>(node.operation)));
            append_field(identity, std::to_string(node.width));
            append_field(identity, node.is_signed ? "signed" : "unsigned");
            append_field(identity, node.constant.to_msb_string());
            append_field(identity, std::to_string(node.terminal.signal));
            append_field(identity, std::to_string(node.terminal.offset));
            append_field(identity, std::to_string(node.terminal.width));
            append_field(identity,
                std::to_string(static_cast<unsigned>(node.binary)));
            append_field(identity,
                std::to_string(static_cast<unsigned>(node.logical)));
            append_field(identity,
                std::to_string(static_cast<unsigned>(node.shift)));
            append_field(identity,
                std::to_string(static_cast<unsigned>(node.reduction)));
            for (const auto operand : node.operands)
                append_field(identity, std::to_string(operand));
            identity.push_back('|');
        }
    }

    [[nodiscard]] std::string condition_program_identity(
        const ModuleTimingCheck& check)
    {
        std::string result = "sdf-elaborated-condition-program-v1";
        append_expression_identity(result, check.reference.condition);
        if (check.data)
            append_expression_identity(result, check.data->condition);
        else
            append_field(result, "no-data-event");
        append_expression_identity(result, check.timestamp_condition);
        append_expression_identity(result, check.timecheck_condition);
        return result;
    }

    [[nodiscard]] bool collect_condition_signals(
        const ModuleTimingCheck& check,
        const std::span<const runtime::simir::Signal> signals,
        const SdfConditionTimingLimits& limits,
        std::vector<SignalId>& condition_signals)
    {
        const std::array<const ModulePathExpression*, 4U> expressions {
            &check.reference.condition,
            check.data ? &check.data->condition : nullptr,
            &check.timestamp_condition,
            &check.timecheck_condition,
        };
        std::size_t node_count { };
        for (const auto* expression : expressions) {
            if (expression == nullptr)
                continue;
            if (expression->nodes.size()
                > limits.max_expression_nodes_per_check - node_count) {
                return false;
            }
            node_count += expression->nodes.size();
            for (const auto& node : expression->nodes) {
                if (node.operation != ModulePathExpressionOperator::terminal)
                    continue;
                if (node.terminal.signal >= signals.size()
                    || node.terminal.width == 0U
                    || node.terminal.offset
                        > signals[node.terminal.signal].initial_value.width()
                    || node.terminal.width
                        > signals[node.terminal.signal].initial_value.width()
                            - node.terminal.offset) {
                    return false;
                }
                condition_signals.push_back(node.terminal.signal);
            }
        }
        std::ranges::sort(condition_signals);
        condition_signals.erase(std::ranges::unique(condition_signals).begin(),
            condition_signals.end());
        return condition_signals.size()
            <= limits.max_condition_signals_per_check;
    }

    [[nodiscard]] std::vector<SignalId> annotation_condition_signals(
        const SdfPlannedAnnotation& annotation)
    {
        std::vector<SignalId> result;
        for (const auto& endpoint : annotation.endpoints) {
            if (endpoint.role == SdfEndpointRole::Condition)
                result.push_back(endpoint.signal);
        }
        std::ranges::sort(result);
        result.erase(std::ranges::unique(result).begin(), result.end());
        return result;
    }

    [[nodiscard]] std::string applied_identity(
        const SdfPlannedAnnotation& annotation,
        const SdfAppliedConditionTiming& applied)
    {
        std::string result = "sdf-condition-timing-v1";
        append_field(result, annotation.canonical_identity);
        append_field(result, applied.effective_check.identity);
        append_field(result, std::to_string(applied.check_id));
        append_field(result, std::to_string(applied.stable_order));
        append_field(result,
            std::to_string(static_cast<unsigned>(
                applied.condition_disposition)));
        append_field(result, applied.condition_program_identity);
        append_field(result,
            applied.notifier ? std::to_string(*applied.notifier) : "none");
        append_field(result, applied.edge_qualified ? "edge" : "level");
        for (const auto signal : applied.condition_signals)
            append_field(result, std::to_string(signal));
        for (const auto limit : applied.effective_check.limits)
            append_field(result, std::to_string(limit));
        return result;
    }

    [[nodiscard]] std::string application_identity(
        const SdfAnnotationPlan& plan,
        const std::vector<SdfAppliedConditionTiming>& checks)
    {
        std::string result = "sdf-condition-timing-application-v1";
        append_field(result, plan.semantic_identity());
        for (const auto& check : checks)
            append_field(result, check.canonical_identity);
        return result;
    }
} // namespace

const std::shared_ptr<const SdfAnnotationPlan>&
SdfConditionTimingApplication::plan() const noexcept
{
    return plan_;
}

std::span<const SdfAppliedConditionTiming>
SdfConditionTimingApplication::checks() const noexcept
{
    return checks_;
}

const SdfAppliedConditionTiming* SdfConditionTimingApplication::find_check(
    const std::uint32_t id) const noexcept
{
    const auto found
        = std::ranges::find(checks_, id, &SdfAppliedConditionTiming::check_id);
    return found == checks_.end() ? nullptr : &*found;
}

std::string_view SdfConditionTimingApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

SdfConditionTimingApplication::SdfConditionTimingApplication(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    std::vector<SdfAppliedConditionTiming> checks,
    std::string semantic_identity)
    : plan_(std::move(plan))
    , checks_(std::move(checks))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfConditionTimingResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfConditionTimingResult apply_sdf_condition_timing(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfConditionTimingLimits limits)
{
    SdfConditionTimingResult result;
    if (!plan || !plan->summary() || !plan->summary()->endpoint_resolution()
        || !plan->summary()->endpoint_resolution()->cells()
        || !plan->summary()->endpoint_resolution()->cells()->scope()
        || !plan->summary()->endpoint_resolution()->cells()->scope()->normalized_ir()
        || plan->summary()->semantic_identity().empty()
        || plan->semantic_identity().empty()
        || plan->summary()->annotation_count() != plan->annotations().size()
        || limits.max_checks == 0U
        || limits.max_expression_nodes_per_check == 0U
        || limits.max_condition_signals_per_check == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-001",
            "Conditional SDF timing application requires a complete plan and nonzero limits",
            { });
        return result;
    }
    const auto design_state = elaborated.state();
    std::vector<SdfAppliedConditionTiming> checks;
    std::set<std::uint32_t> check_ids;
    std::size_t identity_bytes { };
    for (const auto& annotation : plan->annotations()) {
        if (annotation.target_kind != SdfTimingTargetKind::TimingCheck)
            continue;
        const auto* check = find_check(elaborated, annotation.target_identity);
        if (!check || !kind_matches(annotation.construct_kind, check->kind)
            || !check_ids.insert(check ? check->id : 0U).second
            || annotation.before_check_ticks != check->limits) {
            diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-001",
                "Conditional SDF timing plan refers to a missing, repeated, stale, or kind-mismatched check",
                annotation.source);
            continue;
        }
        if (!event_roles_match(annotation, *check)
            || !edge_binding_matches(annotation, *check)) {
            diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-002",
                "SDF timing-check event roles or edge qualifiers do not match elaboration",
                annotation.source);
            continue;
        }
        std::vector<SignalId> condition_signals;
        if (!collect_condition_signals(*check, design_state.signals, limits,
                condition_signals)) {
            diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-004",
                "SDF timing-check condition program exceeds a resource limit or contains a stale terminal",
                annotation.source);
            continue;
        }
        const auto requested_signals
            = annotation_condition_signals(annotation);
        const bool sdf_condition = !annotation.condition_identity.empty();
        if ((sdf_condition
                && (requested_signals.empty()
                    || requested_signals != condition_signals))
            || (!sdf_condition && !requested_signals.empty())) {
            diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-003",
                "SDF timing-check condition is absent or does not match the elaborated condition program",
                annotation.source);
            continue;
        }
        if (!valid_limits(check->kind, check->limits)
            || !valid_limits(check->kind, annotation.after_check_ticks)) {
            diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-004",
                "SDF timing-check has an illegal signed runtime window",
                annotation.source);
            continue;
        }
        if (check->notifier
            && (*check->notifier >= design_state.signals.size()
                || design_state.signals[*check->notifier].initial_value.width()
                    != 1U)) {
            diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-004",
                "SDF timing-check notifier binding is missing or nonscalar",
                annotation.source);
            continue;
        }
        if (checks.size() >= limits.max_checks) {
            diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-004",
                "Conditional SDF timing application exceeds its check limit",
                annotation.source);
            continue;
        }
        SdfAppliedConditionTiming applied;
        applied.check_id = check->id;
        applied.stable_order = static_cast<runtime::StableOrder>(check->id);
        applied.condition_disposition = sdf_condition
            ? SdfTimingConditionDisposition::SdfMatched
            : condition_signals.empty()
            ? SdfTimingConditionDisposition::Unconditional
            : SdfTimingConditionDisposition::ElaboratedOnly;
        applied.condition_signals = std::move(condition_signals);
        applied.notifier = check->notifier;
        applied.edge_qualified = event_is_controlled(check->reference)
            || (check->data && event_is_controlled(*check->data));
        applied.negative_limits = std::ranges::any_of(
            annotation.after_check_ticks,
            [](const auto value) { return value < 0; });
        applied.effective_check = *check;
        applied.effective_check.limits = annotation.after_check_ticks;
        applied.annotation_source = annotation.source;
        applied.annotation_identity = annotation.canonical_identity;
        applied.condition_program_identity
            = condition_program_identity(applied.effective_check);
        applied.canonical_identity = applied_identity(annotation, applied);
        if (applied.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes
                > limits.max_identity_bytes
                    - applied.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-004",
                "Conditional SDF timing application exceeds its identity-byte limit",
                annotation.source);
            continue;
        }
        identity_bytes += applied.canonical_identity.size();
        checks.push_back(std::move(applied));
    }
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(*plan, checks);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-CONDITION-CHECK-004",
            "Conditional SDF timing semantic identity exceeds its configured limit",
            { });
        return result;
    }
    result.application
        = std::make_shared<const SdfConditionTimingApplication>(
            std::move(plan), std::move(checks), identity);
    return result;
}

} // namespace fsim::app
