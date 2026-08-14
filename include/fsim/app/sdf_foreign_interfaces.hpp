// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_mixed_resolution.hpp"
#include "fsim/app/sdf_vital_reannotation.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfForeignInterfaceKind : std::uint8_t {
    Vhpi,
    Vpi,
};

enum class SdfForeignTimingKind : std::uint8_t {
    VitalDelay,
    VitalTimingCheck,
    MixedBoundary,
};

enum class SdfForeignInterfaceError : std::uint8_t {
    None,
    InvalidApplication,
    InvalidRequest,
    NotFound,
    ReadOnly,
    ObservationDisabled,
    CallbackFailed,
    ResourceLimit,
};

enum class SdfForeignValueControl : std::uint8_t {
    Deposit,
    Force,
    Release,
};

struct SdfForeignTimingObject {
    SdfForeignInterfaceKind interface_kind { SdfForeignInterfaceKind::Vhpi };
    SdfForeignTimingKind timing_kind { SdfForeignTimingKind::VitalDelay };
    std::uint64_t handle { };
    std::uint64_t generation { };
    std::string root_identity;
    std::string library_identity;
    std::string hierarchy_path;
    std::string source_identity;
    std::vector<runtime::SimulationTick> effective_ticks;
    std::string canonical_identity;

    friend bool operator==(const SdfForeignTimingObject&,
        const SdfForeignTimingObject&) = default;
};

class SdfForeignInterfaceApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfForeignInterfaceApplication(std::vector<SdfForeignTimingObject> objects,
        std::string semantic_identity);

    [[nodiscard]] std::span<const SdfForeignTimingObject> objects() const
        noexcept;
    [[nodiscard]] const SdfForeignTimingObject* find_identity(
        std::string_view identity) const noexcept;
    [[nodiscard]] const SdfForeignTimingObject* find_handle(
        SdfForeignInterfaceKind interface_kind,
        std::uint64_t handle) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::vector<SdfForeignTimingObject> objects_;
    std::string semantic_identity_;
};

struct SdfForeignInterfaceLimits {
    std::size_t max_objects { 1'000'000U };
    std::size_t max_values { 6'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfForeignInterfaceResult {
    std::shared_ptr<const SdfForeignInterfaceApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfForeignInterfaceResult publish_sdf_foreign_interfaces(
    std::shared_ptr<const SdfVitalReannotationApplication> vital,
    std::shared_ptr<const SdfMixedResolutionApplication> mixed,
    SdfForeignInterfaceLimits limits = { });

struct SdfForeignEnumerationResult {
    std::vector<SdfForeignTimingObject> objects;
    SdfForeignInterfaceError error { SdfForeignInterfaceError::None };

    [[nodiscard]] explicit operator bool() const noexcept;
};

struct SdfForeignObservationEvent {
    std::string object_identity;
    SdfForeignInterfaceKind interface_kind { SdfForeignInterfaceKind::Vhpi };
    std::uint64_t handle { };
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    std::vector<runtime::SimulationTick> effective_ticks;
};

using SdfForeignObservationCallback
    = std::function<void(const SdfForeignObservationEvent&)>;

struct SdfForeignCallbackResult {
    std::uint64_t identity { };
    SdfForeignInterfaceError error { SdfForeignInterfaceError::None };

    [[nodiscard]] explicit operator bool() const noexcept;
};

class SdfForeignObservationSession final {
public:
    explicit SdfForeignObservationSession(
        std::shared_ptr<const SdfForeignInterfaceApplication> application,
        std::size_t max_callbacks = 4096U);

    [[nodiscard]] SdfForeignEnumerationResult enumerate(
        SdfForeignInterfaceKind interface_kind,
        std::size_t offset,
        std::size_t maximum) const;
    [[nodiscard]] SdfForeignCallbackResult register_callback(
        std::string_view object_identity,
        SdfForeignObservationCallback callback);
    [[nodiscard]] SdfForeignInterfaceError remove_callback(
        std::uint64_t identity) noexcept;
    [[nodiscard]] SdfForeignInterfaceError publish(
        std::string_view object_identity,
        runtime::SimulationTick time,
        std::uint64_t delta) noexcept;
    [[nodiscard]] SdfForeignInterfaceError control_value(
        std::string_view object_identity,
        SdfForeignValueControl control,
        std::span<const runtime::SimulationTick> values = { }) const noexcept;
    void set_observation_enabled(bool enabled) noexcept;
    [[nodiscard]] bool observation_enabled() const noexcept;
    [[nodiscard]] std::size_t callbacks() const noexcept;

private:
    struct CallbackEntry {
        std::uint64_t identity { };
        std::string object_identity;
        SdfForeignObservationCallback callback;
    };

    std::shared_ptr<const SdfForeignInterfaceApplication> application_;
    std::size_t max_callbacks_ { };
    std::uint64_t next_callback_ { 1U };
    bool observation_enabled_ { true };
    std::vector<CallbackEntry> callbacks_;
};

} // namespace fsim::app
