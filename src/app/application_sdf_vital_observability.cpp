// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_observability.hpp"

#include <algorithm>
#include <ranges>
#include <set>
#include <tuple>
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

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] std::string vcd_name(const SdfForeignTimingObject& timing)
    {
        return "sdf_vital_"
            + std::to_string(static_cast<unsigned>(timing.interface_kind)) + "_"
            + std::to_string(timing.handle);
    }

    [[nodiscard]] bool valid_region(const runtime::SchedulerPhase region)
    {
        return region >= runtime::SchedulerPhase::active
            && region <= runtime::SchedulerPhase::postponed;
    }
} // namespace

SdfVitalObservabilityApplication::SdfVitalObservabilityApplication(
    std::shared_ptr<const SdfForeignInterfaceApplication> foreign,
    std::vector<SdfVitalObservedObject> objects, std::string semantic_identity)
    : foreign_(std::move(foreign))
    , objects_(std::move(objects))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfForeignInterfaceApplication>&
SdfVitalObservabilityApplication::foreign() const noexcept
{
    return foreign_;
}

std::span<const SdfVitalObservedObject>
SdfVitalObservabilityApplication::objects() const noexcept
{
    return objects_;
}

const SdfVitalObservedObject* SdfVitalObservabilityApplication::find_object(
    const std::string_view identity) const noexcept
{
    const auto found = std::ranges::lower_bound(
        objects_, identity, { }, &SdfVitalObservedObject::canonical_identity);
    return found != objects_.end() && found->canonical_identity == identity
        ? &*found
        : nullptr;
}

std::string_view SdfVitalObservabilityApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

bool SdfVitalObservabilityResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfVitalObservabilityResult build_sdf_vital_observability(
    std::shared_ptr<const SdfForeignInterfaceApplication> foreign,
    const SdfVitalObservabilityLimits limits)
{
    SdfVitalObservabilityResult result;
    if (!foreign || foreign->objects().empty()
        || foreign->semantic_identity().empty() || limits.max_objects == 0U
        || limits.max_values == 0U || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-OBSERVE-001",
            "VITAL observability requires a complete foreign application and nonzero limits");
        return result;
    }
    if (foreign->objects().size() > limits.max_objects) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-OBSERVE-004",
            "VITAL observability exceeds its configured object limit");
        return result;
    }
    std::vector<SdfVitalObservedObject> objects;
    std::set<std::string> identities;
    std::size_t values { };
    std::size_t identity_bytes { };
    for (const auto& timing : foreign->objects()) {
        if (timing.handle == 0U || timing.canonical_identity.empty()
            || timing.effective_ticks.empty()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-OBSERVE-003",
                "VITAL observability contains a stale or incomplete foreign object");
            return result;
        }
        if (!identities.insert(timing.canonical_identity).second) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-OBSERVE-002",
                "VITAL observability repeats a foreign timing identity");
            return result;
        }
        if (timing.effective_ticks.size() > limits.max_values - values) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-OBSERVE-004",
                "VITAL observability exceeds its configured value limit");
            return result;
        }
        SdfVitalObservedObject object;
        object.timing = timing;
        object.canonical_identity = timing.canonical_identity;
        object.vcd_name = vcd_name(timing);
        if (object.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes
                > limits.max_identity_bytes - object.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-OBSERVE-004",
                "VITAL observability exceeds its identity-byte limit");
            return result;
        }
        values += timing.effective_ticks.size();
        identity_bytes += object.canonical_identity.size();
        objects.push_back(std::move(object));
    }
    std::ranges::sort(objects, { }, &SdfVitalObservedObject::canonical_identity);
    std::string identity = "sdf-vital-observability-v1";
    append_field(identity, foreign->semantic_identity());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        objects[index].stable_id = index + 1U;
        append_field(identity, objects[index].canonical_identity);
    }
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-OBSERVE-004",
            "VITAL observability application identity exceeds its limit");
        return result;
    }
    result.application
        = std::make_shared<const SdfVitalObservabilityApplication>(
            std::move(foreign), std::move(objects), std::move(identity));
    return result;
}

SdfVitalObservationRecorder::SdfVitalObservationRecorder(
    std::shared_ptr<const SdfVitalObservabilityApplication> application,
    const std::span<const SdfVitalObservationSurface> enabled_surfaces,
    const std::size_t max_events_per_surface)
    : application_(std::move(application))
    , max_events_per_surface_(max_events_per_surface)
{
    for (const auto surface : enabled_surfaces) {
        if (surface <= SdfVitalObservationSurface::Vcd)
            enabled_mask_ |= static_cast<std::uint8_t>(
                1U << static_cast<unsigned>(surface));
    }
    if (!application_ || max_events_per_surface_ == 0U)
        enabled_mask_ = 0U;
    for (std::size_t index = 0; index < events_.size(); ++index) {
        if ((enabled_mask_ & static_cast<std::uint8_t>(1U << index)) != 0U)
            events_[index].reserve(max_events_per_surface_);
    }
}

SdfVitalObservationStatus SdfVitalObservationRecorder::record(
    const SdfVitalObservationEventKind kind,
    const std::string_view object_identity,
    const runtime::SimulationTick time, const std::uint64_t delta,
    const runtime::SchedulerPhase region,
    const runtime::SimulationTick transaction_time,
    const std::span<const runtime::SimulationTick> before,
    const std::span<const runtime::SimulationTick> after) noexcept
{
    if (enabled_mask_ == 0U)
        return SdfVitalObservationStatus::Disabled;
    const auto* object = application_->find_object(object_identity);
    if (object == nullptr)
        return SdfVitalObservationStatus::UnknownObject;
    if (kind > SdfVitalObservationEventKind::Violation || !valid_region(region)
        || before.size() > SdfVitalObservationEvent::max_values
        || after.size() > SdfVitalObservationEvent::max_values
        || (kind == SdfVitalObservationEventKind::PendingTransaction
            && transaction_time < time)
        || (kind == SdfVitalObservationEventKind::Violation
            && (before.empty() || before.size() != after.size()))) {
        return SdfVitalObservationStatus::InvalidEvent;
    }
    const auto coordinate = std::tuple { time, delta, region };
    if (has_last_
        && coordinate < std::tuple { last_time_, last_delta_, last_region_ }) {
        return SdfVitalObservationStatus::OutOfOrder;
    }
    for (std::size_t index = 0; index < events_.size(); ++index) {
        if ((enabled_mask_ & static_cast<std::uint8_t>(1U << index)) != 0U
            && events_[index].size() == max_events_per_surface_) {
            return SdfVitalObservationStatus::LimitReached;
        }
    }
    SdfVitalObservationEvent event;
    event.sequence = next_sequence_++;
    event.object_id = object->stable_id;
    event.kind = kind;
    event.time = time;
    event.delta = delta;
    event.region = region;
    event.transaction_time = transaction_time;
    event.before_count = static_cast<std::uint8_t>(before.size());
    event.after_count = static_cast<std::uint8_t>(after.size());
    std::ranges::copy(before, event.before.begin());
    std::ranges::copy(after, event.after.begin());
    for (std::size_t index = 0; index < events_.size(); ++index) {
        if ((enabled_mask_ & static_cast<std::uint8_t>(1U << index)) != 0U)
            events_[index].push_back(event);
    }
    last_time_ = time;
    last_delta_ = delta;
    last_region_ = region;
    has_last_ = true;
    return SdfVitalObservationStatus::Recorded;
}

bool SdfVitalObservationRecorder::enabled() const noexcept
{
    return enabled_mask_ != 0U;
}

std::size_t SdfVitalObservationRecorder::reserved_event_slots() const noexcept
{
    std::size_t result { };
    for (const auto& events : events_)
        result += events.capacity();
    return result;
}

std::span<const SdfVitalObservationEvent> SdfVitalObservationRecorder::events(
    const SdfVitalObservationSurface surface) const noexcept
{
    const auto index = static_cast<std::size_t>(surface);
    return index < events_.size()
        ? std::span<const SdfVitalObservationEvent> { events_[index] }
        : std::span<const SdfVitalObservationEvent> { };
}

} // namespace fsim::app
