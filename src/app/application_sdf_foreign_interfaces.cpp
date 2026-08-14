// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_foreign_interfaces.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SourceSpan;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span = { })
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

    [[nodiscard]] bool has_vpi_side(const SdfMixedResolvedBoundary& boundary)
    {
        const auto supported = [](const SdfScopeRootLanguage language) {
            return language == SdfScopeRootLanguage::Verilog
                || language == SdfScopeRootLanguage::SystemVerilog;
        };
        return supported(boundary.source_language)
            || supported(boundary.destination_language);
    }

    [[nodiscard]] std::string object_identity(
        const SdfForeignTimingObject& object)
    {
        std::string result = "sdf-foreign-timing-object-v1";
        append_field(result,
            std::to_string(static_cast<unsigned>(object.interface_kind)));
        append_field(result,
            std::to_string(static_cast<unsigned>(object.timing_kind)));
        append_field(result, object.root_identity);
        append_field(result, object.library_identity);
        append_field(result, object.hierarchy_path);
        append_field(result, object.source_identity);
        return result;
    }

    [[nodiscard]] bool add_object(std::vector<SdfForeignTimingObject>& objects,
        std::set<std::string>& identities, SdfForeignTimingObject object,
        const SdfForeignInterfaceLimits& limits, std::size_t& value_count,
        std::size_t& identity_bytes, std::vector<Diagnostic>& diagnostics)
    {
        if (objects.size() >= limits.max_objects
            || object.effective_ticks.size() > limits.max_values - value_count) {
            diagnose(diagnostics, "FSIM-SDF-FOREIGN-004",
                "SDF foreign timing exceeds its object or value resource limit");
            return false;
        }
        if (object.root_identity.empty() || object.library_identity.empty()
            || object.hierarchy_path.empty() || object.source_identity.empty()
            || object.effective_ticks.empty()) {
            diagnose(diagnostics, "FSIM-SDF-FOREIGN-003",
                "SDF foreign timing contains an incomplete or stale effective object");
            return false;
        }
        object.canonical_identity = object_identity(object);
        if (!identities.insert(object.canonical_identity).second) {
            diagnose(diagnostics, "FSIM-SDF-FOREIGN-002",
                "SDF foreign timing contains a duplicate stable object identity");
            return false;
        }
        if (object.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes
                > limits.max_identity_bytes - object.canonical_identity.size()) {
            diagnose(diagnostics, "FSIM-SDF-FOREIGN-004",
                "SDF foreign timing identities exceed their byte resource limit");
            return false;
        }
        value_count += object.effective_ticks.size();
        identity_bytes += object.canonical_identity.size();
        objects.push_back(std::move(object));
        return true;
    }

    using RevisionMap
        = std::map<std::string_view, const SdfVitalReannotationRevision*,
            std::less<>>;

    [[nodiscard]] RevisionMap revisions(
        const SdfVitalReannotationApplication& vital)
    {
        RevisionMap result;
        for (const auto& revision : vital.revisions())
            result.emplace(revision.target_identity, &revision);
        return result;
    }

    template <typename Timing>
    [[nodiscard]] SdfForeignTimingObject vital_object(const Timing& timing,
        const SdfForeignTimingKind kind, const std::uint64_t generation,
        const RevisionMap& by_target)
    {
        SdfForeignTimingObject object;
        object.interface_kind = SdfForeignInterfaceKind::Vhpi;
        object.timing_kind = kind;
        object.generation = generation;
        object.source_identity = timing.canonical_identity;
        const auto found = by_target.find(timing.canonical_identity);
        if (found != by_target.end()) {
            object.root_identity = found->second->root;
            object.hierarchy_path = found->second->cell_pattern;
        } else {
            object.root_identity = "vhdl";
            object.hierarchy_path = timing.call.canonical_identity;
        }
        object.library_identity = "work";
        return object;
    }

    [[nodiscard]] std::string application_identity(
        const SdfVitalReannotationApplication& vital,
        const SdfMixedResolutionApplication& mixed,
        const std::vector<SdfForeignTimingObject>& objects)
    {
        std::string result = "sdf-foreign-interface-application-v1";
        append_field(result, vital.semantic_identity());
        append_field(result, mixed.semantic_identity());
        append_field(result, std::to_string(vital.generation()));
        for (const auto& object : objects) {
            append_field(result, object.canonical_identity);
            for (const auto tick : object.effective_ticks)
                append_field(result, std::to_string(tick));
        }
        return result;
    }
} // namespace

SdfForeignInterfaceApplication::SdfForeignInterfaceApplication(
    std::vector<SdfForeignTimingObject> objects, std::string semantic_identity)
    : objects_(std::move(objects))
    , semantic_identity_(std::move(semantic_identity))
{
}

std::span<const SdfForeignTimingObject>
SdfForeignInterfaceApplication::objects() const noexcept
{
    return objects_;
}

const SdfForeignTimingObject* SdfForeignInterfaceApplication::find_identity(
    const std::string_view identity) const noexcept
{
    const auto found = std::ranges::find(
        objects_, identity, &SdfForeignTimingObject::canonical_identity);
    return found == objects_.end() ? nullptr : &*found;
}

const SdfForeignTimingObject* SdfForeignInterfaceApplication::find_handle(
    const SdfForeignInterfaceKind interface_kind,
    const std::uint64_t handle) const noexcept
{
    const auto found = std::ranges::find_if(objects_, [&](const auto& object) {
        return object.interface_kind == interface_kind
            && object.handle == handle;
    });
    return found == objects_.end() ? nullptr : &*found;
}

std::string_view SdfForeignInterfaceApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

bool SdfForeignInterfaceResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfForeignInterfaceResult publish_sdf_foreign_interfaces(
    std::shared_ptr<const SdfVitalReannotationApplication> vital,
    std::shared_ptr<const SdfMixedResolutionApplication> mixed,
    const SdfForeignInterfaceLimits limits)
{
    SdfForeignInterfaceResult result;
    if (!vital || !mixed || vital->generation() == 0U
        || vital->semantic_identity().empty() || mixed->semantic_identity().empty()
        || limits.max_objects == 0U || limits.max_values == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-FOREIGN-001",
            "SDF foreign timing requires complete VITAL/mixed applications and nonzero limits");
        return result;
    }

    std::vector<SdfForeignTimingObject> objects;
    std::set<std::string> identities;
    std::size_t value_count { };
    std::size_t identity_bytes { };
    const auto by_target = revisions(*vital);
    for (const auto& timing : vital->delays()) {
        auto object = vital_object(timing, SdfForeignTimingKind::VitalDelay,
            vital->generation(), by_target);
        object.effective_ticks = timing.effective_delay_ticks;
        (void)add_object(objects, identities, std::move(object), limits,
            value_count, identity_bytes, result.diagnostics);
    }
    for (const auto& timing : vital->checks()) {
        auto object = vital_object(timing,
            SdfForeignTimingKind::VitalTimingCheck, vital->generation(),
            by_target);
        object.effective_ticks.assign(
            timing.effective_limits.begin(), timing.effective_limits.end());
        (void)add_object(objects, identities, std::move(object), limits,
            value_count, identity_bytes, result.diagnostics);
    }
    for (const auto& boundary : mixed->boundaries()) {
        const auto append = [&](const SdfForeignInterfaceKind interface_kind) {
            SdfForeignTimingObject object;
            object.interface_kind = interface_kind;
            object.timing_kind = SdfForeignTimingKind::MixedBoundary;
            object.generation = vital->generation();
            object.root_identity = boundary.root_identity;
            object.library_identity = boundary.library_identity;
            object.hierarchy_path = boundary.boundary_path.empty()
                ? boundary.source_path + "->" + boundary.destination_path
                : boundary.boundary_path;
            object.source_identity = boundary.canonical_identity;
            object.effective_ticks = boundary.effective_ticks;
            (void)add_object(objects, identities, std::move(object), limits,
                value_count, identity_bytes, result.diagnostics);
        };
        append(SdfForeignInterfaceKind::Vhpi);
        if (has_vpi_side(boundary))
            append(SdfForeignInterfaceKind::Vpi);
    }
    if (!result.diagnostics.empty())
        return result;

    std::ranges::sort(objects, { }, [](const auto& object) {
        return std::tuple { object.interface_kind,
            object.canonical_identity };
    });
    std::array<std::uint64_t, 2> next_handle { 1U, 1U };
    for (auto& object : objects) {
        auto& next = next_handle.at(
            static_cast<std::size_t>(object.interface_kind));
        object.handle = next++;
    }
    const auto identity = application_identity(*vital, *mixed, objects);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-FOREIGN-004",
            "SDF foreign timing application identity exceeds its byte limit");
        return result;
    }
    result.application = std::make_shared<const SdfForeignInterfaceApplication>(
        std::move(objects), identity);
    return result;
}

SdfForeignEnumerationResult::operator bool() const noexcept
{
    return error == SdfForeignInterfaceError::None;
}

SdfForeignCallbackResult::operator bool() const noexcept
{
    return error == SdfForeignInterfaceError::None && identity != 0U;
}

SdfForeignObservationSession::SdfForeignObservationSession(
    std::shared_ptr<const SdfForeignInterfaceApplication> application,
    const std::size_t max_callbacks)
    : application_(std::move(application))
    , max_callbacks_(max_callbacks)
{
}

SdfForeignEnumerationResult SdfForeignObservationSession::enumerate(
    const SdfForeignInterfaceKind interface_kind, const std::size_t offset,
    const std::size_t maximum) const
{
    SdfForeignEnumerationResult result;
    if (!application_) {
        result.error = SdfForeignInterfaceError::InvalidApplication;
        return result;
    }
    if (maximum == 0U) {
        result.error = SdfForeignInterfaceError::InvalidRequest;
        return result;
    }
    std::size_t skipped { };
    for (const auto& object : application_->objects()) {
        if (object.interface_kind != interface_kind)
            continue;
        if (skipped++ < offset)
            continue;
        result.objects.push_back(object);
        if (result.objects.size() == maximum)
            break;
    }
    return result;
}

SdfForeignCallbackResult SdfForeignObservationSession::register_callback(
    const std::string_view object_identity,
    SdfForeignObservationCallback callback)
{
    SdfForeignCallbackResult result;
    if (!application_) {
        result.error = SdfForeignInterfaceError::InvalidApplication;
    } else if (application_->find_identity(object_identity) == nullptr
        || !callback) {
        result.error = SdfForeignInterfaceError::InvalidRequest;
    } else if (callbacks_.size() >= max_callbacks_ || next_callback_ == 0U) {
        result.error = SdfForeignInterfaceError::ResourceLimit;
    } else {
        result.identity = next_callback_++;
        callbacks_.push_back(CallbackEntry { result.identity,
            std::string { object_identity }, std::move(callback) });
    }
    return result;
}

SdfForeignInterfaceError SdfForeignObservationSession::remove_callback(
    const std::uint64_t identity) noexcept
{
    const auto old_size = callbacks_.size();
    std::erase_if(callbacks_,
        [&](const auto& entry) { return entry.identity == identity; });
    return old_size == callbacks_.size()
        ? SdfForeignInterfaceError::NotFound
        : SdfForeignInterfaceError::None;
}

SdfForeignInterfaceError SdfForeignObservationSession::publish(
    const std::string_view object_identity, const runtime::SimulationTick time,
    const std::uint64_t delta) noexcept
{
    if (!application_)
        return SdfForeignInterfaceError::InvalidApplication;
    const auto* object = application_->find_identity(object_identity);
    if (object == nullptr)
        return SdfForeignInterfaceError::NotFound;
    if (!observation_enabled_)
        return SdfForeignInterfaceError::ObservationDisabled;
    const SdfForeignObservationEvent event { object->canonical_identity,
        object->interface_kind, object->handle, time, delta,
        object->effective_ticks };
    try {
        for (const auto& entry : callbacks_) {
            if (entry.object_identity == object_identity)
                entry.callback(event);
        }
    } catch (...) {
        return SdfForeignInterfaceError::CallbackFailed;
    }
    return SdfForeignInterfaceError::None;
}

SdfForeignInterfaceError SdfForeignObservationSession::control_value(
    const std::string_view object_identity,
    const SdfForeignValueControl control,
    const std::span<const runtime::SimulationTick> values) const noexcept
{
    if (!application_)
        return SdfForeignInterfaceError::InvalidApplication;
    if (application_->find_identity(object_identity) == nullptr)
        return SdfForeignInterfaceError::NotFound;
    const bool release = control == SdfForeignValueControl::Release;
    if ((release && !values.empty()) || (!release && values.empty()))
        return SdfForeignInterfaceError::InvalidRequest;
    return SdfForeignInterfaceError::ReadOnly;
}

void SdfForeignObservationSession::set_observation_enabled(
    const bool enabled) noexcept
{
    observation_enabled_ = enabled;
}

bool SdfForeignObservationSession::observation_enabled() const noexcept
{
    return observation_enabled_;
}

std::size_t SdfForeignObservationSession::callbacks() const noexcept
{
    return callbacks_.size();
}

} // namespace fsim::app
