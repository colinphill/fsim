// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_vital_models.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfVitalEffectiveValueSource {
    SourceRecord,
    TimingGeneric,
    SdfAbsolute,
    SdfIncrement,
};

struct SdfVitalPrecedencePolicy {
    SdfDelaySelection command_selection { SdfDelaySelection::Typical };
    bool delay_annotations_enabled { true };
    bool timing_check_annotations_enabled { true };

    friend bool operator==(const SdfVitalPrecedencePolicy&,
        const SdfVitalPrecedencePolicy&) = default;
};

struct SdfVitalTimingGenericValue {
    std::string call_identity;
    std::size_t value_index { };
    std::optional<std::uint64_t> delay_ticks;
    std::optional<std::int64_t> check_ticks;
    std::string generic_identity;

    friend bool operator==(const SdfVitalTimingGenericValue&,
        const SdfVitalTimingGenericValue&) = default;
};

struct SdfVitalAnnotationControl {
    std::size_t revision_index { };
    std::string plan_identity;
    bool delay_enabled { true };
    bool timing_check_enabled { true };
    std::string control_identity;

    friend bool operator==(const SdfVitalAnnotationControl&,
        const SdfVitalAnnotationControl&) = default;
};

struct SdfVitalPrecedenceStep {
    std::size_t revision_index { };
    std::string plan_identity;
    SdfDelayApplicationMode mode { SdfDelayApplicationMode::None };
    bool enabled { };
    std::optional<std::uint64_t> delay_ticks;
    std::optional<std::int64_t> check_ticks;
    frontend::SourceSpan source;
    std::string annotation_identity;

    friend bool operator==(const SdfVitalPrecedenceStep&,
        const SdfVitalPrecedenceStep&) = default;
};

struct SdfVitalEffectiveTimingValue {
    std::string call_identity;
    std::size_t value_index { };
    bool timing_check { };
    SdfDelaySelection command_selection { SdfDelaySelection::Typical };
    SdfVitalEffectiveValueSource base_source {
        SdfVitalEffectiveValueSource::SourceRecord
    };
    SdfVitalEffectiveValueSource selected_source {
        SdfVitalEffectiveValueSource::SourceRecord
    };
    std::optional<std::uint64_t> source_delay_ticks;
    std::optional<std::uint64_t> effective_delay_ticks;
    std::optional<std::int64_t> source_check_ticks;
    std::optional<std::int64_t> effective_check_ticks;
    std::string generic_identity;
    bool no_annotation { true };
    std::vector<SdfVitalPrecedenceStep> steps;
    std::string canonical_identity;

    friend bool operator==(const SdfVitalEffectiveTimingValue&,
        const SdfVitalEffectiveTimingValue&) = default;
};

class SdfVitalPrecedenceApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalPrecedenceApplication(
        std::shared_ptr<const SdfVitalModelPlan> source,
        std::vector<std::shared_ptr<const SdfVitalModelPlan>> revisions,
        std::vector<SdfVitalTimingGenericValue> generics,
        std::vector<SdfVitalAnnotationControl> controls,
        SdfVitalPrecedencePolicy policy,
        std::vector<SdfVitalEffectiveTimingValue> values,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfVitalModelPlan>& source()
        const noexcept;
    [[nodiscard]] std::span<const std::shared_ptr<const SdfVitalModelPlan>>
    revisions() const noexcept;
    [[nodiscard]] std::span<const SdfVitalTimingGenericValue> generics() const
        noexcept;
    [[nodiscard]] std::span<const SdfVitalAnnotationControl> controls() const
        noexcept;
    [[nodiscard]] const SdfVitalPrecedencePolicy& policy() const noexcept;
    [[nodiscard]] std::span<const SdfVitalEffectiveTimingValue> values() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfVitalModelPlan> source_;
    std::vector<std::shared_ptr<const SdfVitalModelPlan>> revisions_;
    std::vector<SdfVitalTimingGenericValue> generics_;
    std::vector<SdfVitalAnnotationControl> controls_;
    SdfVitalPrecedencePolicy policy_;
    std::vector<SdfVitalEffectiveTimingValue> values_;
    std::string semantic_identity_;
};

struct SdfVitalPrecedenceLimits {
    std::size_t max_revisions { 4096U };
    std::size_t max_values { 1'000'000U };
    std::size_t max_steps_per_value { 4096U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfVitalPrecedenceResult {
    std::shared_ptr<const SdfVitalPrecedenceApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalPrecedenceResult apply_sdf_vital_precedence(
    std::shared_ptr<const SdfVitalModelPlan> source,
    std::span<const std::shared_ptr<const SdfVitalModelPlan>> revisions = { },
    std::span<const SdfVitalTimingGenericValue> generics = { },
    std::span<const SdfVitalAnnotationControl> controls = { },
    SdfVitalPrecedencePolicy policy = { },
    SdfVitalPrecedenceLimits limits = { });

} // namespace fsim::app
