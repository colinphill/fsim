// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_protocol.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelValueCodecVersion = 1U;

enum class SystemCKernelValueKind : std::uint8_t {
    bit2 = 1,
    logic4 = 2,
    logic9 = 3,
    enumeration = 4,
    time = 5,
};

enum class SystemCKernelRangeDirection : std::uint8_t {
    ascending = 1,
    descending = 2,
};

enum class SystemCKernelValueCode : std::uint16_t {
    none = 0,
    metadata = 1,
    payload = 2,
    resource = 3,
    lossy = 4,
};

struct SystemCKernelValueLimits {
    std::uint32_t max_width_bits { 1U << 20U };
    std::size_t max_encoded_bytes { 16U * 1024U * 1024U };
    std::size_t max_type_name_bytes { 1024U };
    std::size_t max_enum_literals { 4096U };
    std::size_t max_enum_literal_bytes { 1024U };
    std::size_t max_enum_text_bytes { 1024U * 1024U };
};

struct SystemCKernelValueRange {
    std::int64_t left { };
    std::int64_t right { };
    SystemCKernelRangeDirection direction {
        SystemCKernelRangeDirection::descending
    };

    friend auto operator<=>(const SystemCKernelValueRange&,
        const SystemCKernelValueRange&) = default;
};

struct SystemCKernelValue {
    SystemCKernelValueKind kind { SystemCKernelValueKind::bit2 };
    std::uint32_t width { };
    bool is_signed { };
    SystemCKernelValueRange range;
    std::string type_name;
    std::vector<std::string> enum_literals;
    std::uint64_t time_unit_fs { };
    std::vector<std::vector<std::uint64_t>> planes;

    friend bool operator==(const SystemCKernelValue&,
        const SystemCKernelValue&) = default;
};

struct SystemCKernelScalarProjection {
    std::uint64_t bits { };
    std::uint16_t width { };
    bool is_signed { };

    friend bool operator==(const SystemCKernelScalarProjection&,
        const SystemCKernelScalarProjection&) = default;
};

[[nodiscard]] std::size_t systemc_kernel_value_plane_count(
    SystemCKernelValueKind kind) noexcept;

[[nodiscard]] bool validate_systemc_kernel_value(
    const SystemCKernelValue& value, const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_kernel_value(const SystemCKernelValue& value,
    const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<SystemCKernelValue>
deserialize_systemc_kernel_value(std::span<const std::byte> bytes,
    const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<SystemCKernelValue>
make_systemc_kernel_scalar_value(const SystemCKernelScalarProjection& value,
    const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<SystemCKernelScalarProjection>
project_systemc_kernel_scalar_value(const SystemCKernelValue& value,
    const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_value_diagnostic_code(
    SystemCKernelValueCode code) noexcept;

} // namespace fsim::systemc
