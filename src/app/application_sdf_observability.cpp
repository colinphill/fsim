// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_observability.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <utility>

namespace fsim::app {
namespace {

    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), { }, { } });
    }

    std::size_t surface_index(const SdfObservationSurface surface) noexcept
    {
        return static_cast<std::size_t>(surface);
    }

    std::uint8_t surface_bit(const SdfObservationSurface surface) noexcept
    {
        return static_cast<std::uint8_t>(1U << surface_index(surface));
    }

    bool valid_span(const frontend::SourceSpan& span) noexcept
    {
        return !span.source_name.empty() && span.begin.line != 0U
            && span.begin.column != 0U;
    }

    std::string vcd_name(const std::string_view target)
    {
        std::string result = "sdf_";
        result.reserve(result.size() + target.size());
        for (const auto character : target) {
            const auto byte = static_cast<unsigned char>(character);
            result.push_back(std::isalnum(byte) != 0 ? character : '_');
        }
        return result;
    }

    bool valid_region(const runtime::SchedulerPhase region) noexcept
    {
        switch (region) {
        case runtime::SchedulerPhase::active:
        case runtime::SchedulerPhase::inactive:
        case runtime::SchedulerPhase::update:
        case runtime::SchedulerPhase::postponed:
        case runtime::SchedulerPhase::reactive:
        case runtime::SchedulerPhase::observed:
        case runtime::SchedulerPhase::re_inactive:
        case runtime::SchedulerPhase::re_update:
            return true;
        }
        return false;
    }

    std::span<const SdfObservedViolation> event_span(
        const std::array<std::vector<SdfObservedViolation>, 5>& events,
        const SdfObservationSurface surface) noexcept
    {
        return events[surface_index(surface)];
    }

} // namespace

std::span<const SdfObservedObject>
SdfObservabilityApplication::objects() const noexcept
{
    return objects_;
}

const SdfObservedObject* SdfObservabilityApplication::find_object(
    const std::string_view target_identity) const noexcept
{
    const auto found = std::ranges::lower_bound(objects_, target_identity, { },
        &SdfObservedObject::target_identity);
    return found != objects_.end() && found->target_identity == target_identity
        ? &*found
        : nullptr;
}

std::string_view SdfObservabilityApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfObservabilityApplication::SdfObservabilityApplication(
    std::vector<SdfObservedObject> objects, std::string semantic_identity)
    : objects_(std::move(objects))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfObservabilityResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfObservabilityResult build_sdf_observability(
    const std::span<const SdfObservationTarget> targets,
    const SdfObservabilityLimits limits)
{
    SdfObservabilityResult result;
    if (targets.empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-OBSERVE-001",
            "SDF observability requires at least one effective timing target");
        return result;
    }
    if (targets.size() > limits.max_objects || limits.max_objects == 0U
        || limits.max_events_per_surface == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-OBSERVE-004",
            "SDF observability exceeds an object, event or identity limit");
        return result;
    }
    std::set<std::string_view> identities;
    std::vector<SdfObservedObject> objects;
    objects.reserve(targets.size());
    for (const auto& target : targets) {
        if (target.kind > SdfEffectiveValueKind::TimingCheckLimit
            || target.target_identity.empty() || target.source_identity.empty()
            || !valid_span(target.source_span) || target.original_values.empty()
            || target.original_values.size() != target.effective_values.size()
            || target.original_values.size() > SdfObservedViolation::max_values) {
            diagnose(result.diagnostics, "FSIM-SDF-OBSERVE-001",
                "SDF observed target has incomplete identity, span or values");
            return result;
        }
        if (!identities.emplace(target.target_identity).second) {
            diagnose(result.diagnostics, "FSIM-SDF-OBSERVE-002",
                "SDF observability repeats a target identity");
            return result;
        }
        SdfObservedObject object;
        object.kind = target.kind;
        object.target_identity = target.target_identity;
        object.source_identity = target.source_identity;
        object.source_span = target.source_span;
        object.original_values = target.original_values;
        object.effective_values = target.effective_values;
        object.vcd_name = vcd_name(target.target_identity);
        objects.push_back(std::move(object));
    }
    std::ranges::sort(objects, { }, &SdfObservedObject::target_identity);
    std::string identity = "sdf-observability-v1";
    for (std::size_t index = 0; index < objects.size(); ++index) {
        auto& object = objects[index];
        object.stable_id = index + 1U;
        object.vpi_handle = UINT64_C(0x5344460000000000) | object.stable_id;
        object.canonical_identity = "sdf-observed-v1:" + object.target_identity
            + ":" + object.source_identity;
        identity += ":" + object.canonical_identity;
        if (object.canonical_identity.size() > limits.max_identity_bytes
            || identity.size() > limits.max_identity_bytes) {
            diagnose(result.diagnostics, "FSIM-SDF-OBSERVE-004",
                "SDF observed identity exceeds its configured limit");
            return result;
        }
    }
    result.application = std::make_shared<const SdfObservabilityApplication>(
        std::move(objects), std::move(identity));
    return result;
}

std::string_view sdf_observation_status_diagnostic_code(
    const SdfObservationStatus status) noexcept
{
    switch (status) {
    case SdfObservationStatus::UnknownTarget:
    case SdfObservationStatus::InvalidEvent:
        return "FSIM-SDF-OBSERVE-003";
    case SdfObservationStatus::LimitReached:
        return "FSIM-SDF-OBSERVE-004";
    case SdfObservationStatus::Recorded:
    case SdfObservationStatus::Disabled:
        return { };
    }
    return { };
}

SdfObservationRecorder::SdfObservationRecorder(
    std::shared_ptr<const SdfObservabilityApplication> application,
    const std::span<const SdfObservationSurface> enabled_surfaces,
    const std::size_t max_events_per_surface)
    : application_(std::move(application))
    , max_events_per_surface_(max_events_per_surface)
{
    for (const auto surface : enabled_surfaces) {
        if (surface <= SdfObservationSurface::Vcd)
            enabled_mask_ |= surface_bit(surface);
    }
    if (!application_ || max_events_per_surface_ == 0U)
        enabled_mask_ = 0U;
    for (std::size_t index = 0; index < events_.size(); ++index) {
        if ((enabled_mask_ & static_cast<std::uint8_t>(1U << index)) != 0U)
            events_[index].reserve(max_events_per_surface_);
    }
}

SdfObservationStatus SdfObservationRecorder::record_violation(
    const std::string_view target_identity, const runtime::SimulationTick time,
    const std::uint64_t delta, const runtime::SchedulerPhase region,
    const std::span<const std::int64_t> before,
    const std::span<const std::int64_t> after) noexcept
{
    if (enabled_mask_ == 0U)
        return SdfObservationStatus::Disabled;
    const auto* object = application_->find_object(target_identity);
    if (!object)
        return SdfObservationStatus::UnknownTarget;
    if (!valid_region(region) || before.empty() || before.size() != after.size()
        || before.size() > SdfObservedViolation::max_values)
        return SdfObservationStatus::InvalidEvent;
    for (std::size_t index = 0; index < events_.size(); ++index) {
        if ((enabled_mask_ & static_cast<std::uint8_t>(1U << index)) != 0U
            && events_[index].size() == max_events_per_surface_)
            return SdfObservationStatus::LimitReached;
    }
    SdfObservedViolation event;
    event.sequence = next_sequence_++;
    event.object_id = object->stable_id;
    event.time = time;
    event.delta = delta;
    event.region = region;
    event.value_count = static_cast<std::uint8_t>(before.size());
    std::ranges::copy(before, event.before.begin());
    std::ranges::copy(after, event.after.begin());
    for (std::size_t index = 0; index < events_.size(); ++index) {
        if ((enabled_mask_ & static_cast<std::uint8_t>(1U << index)) != 0U)
            events_[index].push_back(event);
    }
    return SdfObservationStatus::Recorded;
}

bool SdfObservationRecorder::enabled() const noexcept
{
    return enabled_mask_ != 0U;
}

std::size_t SdfObservationRecorder::reserved_event_slots() const noexcept
{
    std::size_t result { };
    for (const auto& events : events_)
        result += events.capacity();
    return result;
}

std::span<const SdfObservedViolation>
SdfObservationRecorder::debugger_events() const noexcept
{
    return event_span(events_, SdfObservationSurface::Debugger);
}

std::span<const SdfObservedViolation>
SdfObservationRecorder::callback_events() const noexcept
{
    return event_span(events_, SdfObservationSurface::Callback);
}

std::span<const SdfObservedViolation>
SdfObservationRecorder::internal_trace_events() const noexcept
{
    return event_span(events_, SdfObservationSurface::InternalTrace);
}

std::span<const SdfObservedViolation>
SdfObservationRecorder::vpi_events() const noexcept
{
    return event_span(events_, SdfObservationSurface::Vpi);
}

std::span<const SdfObservedViolation>
SdfObservationRecorder::vcd_events() const noexcept
{
    return event_span(events_, SdfObservationSurface::Vcd);
}

} // namespace fsim::app
