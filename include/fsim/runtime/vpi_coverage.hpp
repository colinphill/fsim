// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_object.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::string_view kSystemVerilogVpiCoverageSchema
    = "fsim-systemverilog-vpi-coverage-v3";
inline constexpr std::string_view kSystemVerilogVpiCoverageDiagnostic
    = "FSIM-COV-040";

enum class SystemVerilogVpiCoverageControl : std::int32_t {
    Start = 750,
    Stop = 751,
    Reset = 752,
    Check = 753,
    Merge = 754,
    Save = 755,
};

enum class SystemVerilogVpiCoverageType : std::int32_t {
    Assertion = 760,
    FsmState = 761,
    Statement = 762,
    Toggle = 763,
};

enum class SystemVerilogVpiCoverageProperty : std::int32_t {
    AssertCoverage = 760,
    FsmStateCoverage = 761,
    StatementCoverage = 762,
    ToggleCoverage = 763,
    Covered = 765,
    CoveredMax = 766,
    CoveredCount = 767,
    AssertAttemptCovered = 770,
    AssertSuccessCovered = 771,
    AssertFailureCovered = 772,
    AssertVacuousSuccessCovered = 773,
    AssertDisableCovered = 774,
    AssertKillCovered = 777,
};

enum class SystemVerilogVpiCoverageRelation : std::int32_t {
    Fsm = 758,
    FsmHandle = 759,
    FsmStates = 775,
    FsmStateExpression = 776,
};

enum class SystemVerilogVpiCoverageObjectKind : std::uint8_t {
    Fsm,
    FsmState,
};

enum class SystemVerilogVpiCoverageError : std::uint8_t {
    None,
    InvalidService,
    InvalidControl,
    InvalidCoverageType,
    InvalidProperty,
    InvalidRelation,
    InvalidRequest,
    InvalidObject,
    CrossSimulation,
    DuplicateObject,
    InvalidStatistics,
    InvalidState,
    DuplicateState,
    NotFound,
    InvalidHandle,
    StaleHandle,
    ReleasedHandle,
    End,
    Overflow,
    ResourceLimit,
    ProviderFailure,
};

struct SystemVerilogVpiCoverageStatistics {
    std::uint64_t coverable_items { };
    std::uint64_t covered_items { };
    std::uint64_t covered_count { };
    std::uint64_t assertion_attempts { };
    std::uint64_t assertion_successes { };
    std::uint64_t assertion_failures { };
    std::uint64_t assertion_vacuous_successes { };
    std::uint64_t assertion_disables { };
    std::uint64_t assertion_kills { };
};

struct SystemVerilogVpiCoverageControlRequest {
    SystemVerilogVpiCoverageControl control {
        SystemVerilogVpiCoverageControl::Check
    };
    SystemVerilogVpiCoverageType type {
        SystemVerilogVpiCoverageType::Statement
    };
    std::optional<fsim_vpi_handle_v1> object;
    std::string filename;
};

struct SystemVerilogVpiCoverageControlResult {
    std::int32_t value { };
    SystemVerilogVpiCoverageError error {
        SystemVerilogVpiCoverageError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiCoverageError::None;
    }
};

struct SystemVerilogVpiCoveragePropertyResult {
    std::int32_t value { };
    SystemVerilogVpiCoverageError error {
        SystemVerilogVpiCoverageError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiCoverageError::None;
    }
};

struct SystemVerilogVpiCoverageHandleResult {
    fsim_vpi_handle_v1 value { };
    SystemVerilogVpiCoverageError error {
        SystemVerilogVpiCoverageError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiCoverageError::None && value != 0U;
    }
};

struct SystemVerilogVpiCoverageValueResult {
    std::optional<SystemVerilogVpiStoredValue> value;
    SystemVerilogVpiCoverageError error {
        SystemVerilogVpiCoverageError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiCoverageError::None
            && value.has_value();
    }
};

struct SystemVerilogVpiCoverageFsmStateDescriptor {
    std::string name;
    SystemVerilogVpiStoredValue value;
};

struct SystemVerilogVpiCoverageFsmDescriptor {
    fsim_vpi_handle_v1 instance { };
    fsim_vpi_handle_v1 state_expression { };
    std::string name;
    std::vector<SystemVerilogVpiCoverageFsmStateDescriptor> states;
};

struct SystemVerilogVpiCoverageLimits {
    std::size_t maximum_targets { 1U << 20U };
    std::size_t maximum_fsms { 1U << 20U };
    std::size_t maximum_states { 1U << 22U };
    std::size_t maximum_iterators { 1U << 16U };
    std::size_t maximum_iterator_objects { 1U << 22U };
    std::size_t maximum_name_bytes { 1U << 16U };
};

using SystemVerilogVpiCoverageStatisticsProvider
    = std::function<std::optional<SystemVerilogVpiCoverageStatistics>(
        fsim_vpi_handle_v1)>;
using SystemVerilogVpiCoverageControlHook
    = std::function<std::int32_t(
        const SystemVerilogVpiCoverageControlRequest&)>;

class SystemVerilogVpiCoverageService final {
public:
    SystemVerilogVpiCoverageService(
        SystemVerilogVpiObjectRegistry& objects,
        SystemVerilogVpiCoverageStatisticsProvider statistics,
        SystemVerilogVpiCoverageControlHook control,
        SystemVerilogVpiCoverageLimits limits = { });
    ~SystemVerilogVpiCoverageService();

    SystemVerilogVpiCoverageService(
        const SystemVerilogVpiCoverageService&) = delete;
    SystemVerilogVpiCoverageService& operator=(
        const SystemVerilogVpiCoverageService&) = delete;
    SystemVerilogVpiCoverageService(
        SystemVerilogVpiCoverageService&&) = delete;
    SystemVerilogVpiCoverageService& operator=(
        SystemVerilogVpiCoverageService&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint64_t simulation_identity() const noexcept;
    [[nodiscard]] SystemVerilogVpiCoverageError publish_target(
        fsim_vpi_handle_v1 object,
        std::span<const SystemVerilogVpiCoverageType> types);
    [[nodiscard]] SystemVerilogVpiCoverageHandleResult publish_fsm(
        SystemVerilogVpiCoverageFsmDescriptor descriptor);
    [[nodiscard]] SystemVerilogVpiCoverageControlResult control(
        const SystemVerilogVpiCoverageControlRequest& request) const;
    [[nodiscard]] SystemVerilogVpiCoveragePropertyResult property(
        SystemVerilogVpiCoverageProperty property,
        fsim_vpi_handle_v1 object) const;
    [[nodiscard]] SystemVerilogVpiCoverageHandleResult handle(
        SystemVerilogVpiCoverageRelation relation,
        fsim_vpi_handle_v1 reference) const;
    [[nodiscard]] SystemVerilogVpiCoverageHandleResult iterate(
        SystemVerilogVpiCoverageRelation relation,
        fsim_vpi_handle_v1 reference);
    [[nodiscard]] SystemVerilogVpiCoverageHandleResult scan(
        fsim_vpi_handle_v1 iterator);
    [[nodiscard]] SystemVerilogVpiCoverageError release_iterator(
        fsim_vpi_handle_v1 iterator);
    [[nodiscard]] SystemVerilogVpiCoverageValueResult value(
        fsim_vpi_handle_v1 state) const;
    [[nodiscard]] std::size_t target_count() const;
    [[nodiscard]] std::size_t fsm_count() const;
    [[nodiscard]] std::size_t state_count() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime
