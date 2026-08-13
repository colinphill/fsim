// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/systemverilog_chandle.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::size_t maximum_dpi_scalar_bits = 1U << 20U;
inline constexpr std::size_t maximum_dpi_string_bytes = 1U << 20U;

enum class SystemVerilogDpiTransferMode {
    Input,
    Output,
    Inout,
    Ref,
};

enum class SystemVerilogDpiScalarDomain {
    Bit2,
    Logic4,
};

enum class SystemVerilogDpiMarshallingError {
    None,
    EmptyWidth,
    WidthLimit,
    WidthMismatch,
    UnknownValue,
    PlaneSize,
    UnusedBits,
    KindMismatch,
    DirectionMismatch,
    Nonfinite,
    InvalidUtf8,
    EmbeddedNul,
    StringLimit,
    StaleHandle,
    InvalidDescriptor,
    LayoutOverflow,
    ElementCount,
    EnumValue,
    DepthLimit,
    InvalidDimension,
    IndexRange,
    Noncontiguous,
    StaleArray,
};

// ABI-neutral owning payload using the 32-bit aval/bval planes required by
// standard DPI bit-vector and logic-vector transfer.
struct SystemVerilogDpiScalarPayload {
    SystemVerilogDpiScalarDomain domain {
        SystemVerilogDpiScalarDomain::Bit2
    };
    std::size_t width { };
    std::vector<std::uint32_t> aval;
    std::vector<std::uint32_t> bval;

    friend bool operator==(
        const SystemVerilogDpiScalarPayload&,
        const SystemVerilogDpiScalarPayload&) = default;
};

struct SystemVerilogDpiScalarMarshalResult {
    SystemVerilogDpiScalarPayload value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

struct SystemVerilogDpiScalarUnmarshalResult {
    PackedLogic4 value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

[[nodiscard]] SystemVerilogDpiScalarMarshalResult
marshal_systemverilog_dpi_scalar(
    const PackedLogic4& value,
    SystemVerilogDpiScalarDomain domain,
    std::size_t maximum_bits = maximum_dpi_scalar_bits);

[[nodiscard]] SystemVerilogDpiScalarUnmarshalResult
unmarshal_systemverilog_dpi_scalar(
    const SystemVerilogDpiScalarPayload& value,
    std::size_t expected_width,
    std::size_t maximum_bits = maximum_dpi_scalar_bits);

enum class SystemVerilogDpiRealKind {
    ShortReal,
    Real,
    Realtime,
};

struct SystemVerilogDpiRealPayload {
    SystemVerilogDpiRealKind kind { SystemVerilogDpiRealKind::Real };
    SystemVerilogDpiTransferMode mode {
        SystemVerilogDpiTransferMode::Input
    };
    std::uint64_t bits { };
};

struct SystemVerilogDpiRealMarshalResult {
    SystemVerilogDpiRealPayload value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

struct SystemVerilogDpiRealUnmarshalResult {
    SystemVerilogScalarValue value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

[[nodiscard]] SystemVerilogDpiRealMarshalResult
marshal_systemverilog_dpi_real(
    const SystemVerilogScalarValue& value,
    SystemVerilogDpiTransferMode mode);

[[nodiscard]] SystemVerilogDpiRealUnmarshalResult
unmarshal_systemverilog_dpi_real(
    const SystemVerilogDpiRealPayload& value,
    SystemVerilogDpiRealKind expected_kind,
    SystemVerilogDpiTransferMode expected_mode);

struct SystemVerilogDpiStringPayload {
    SystemVerilogDpiTransferMode mode {
        SystemVerilogDpiTransferMode::Input
    };
    std::string bytes;
};

struct SystemVerilogDpiStringResult {
    SystemVerilogDpiStringPayload value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

[[nodiscard]] SystemVerilogDpiStringResult
marshal_systemverilog_dpi_string(
    std::string_view value,
    SystemVerilogDpiTransferMode mode,
    std::size_t maximum_bytes = maximum_dpi_string_bytes);

[[nodiscard]] SystemVerilogDpiStringResult
unmarshal_systemverilog_dpi_string(
    const SystemVerilogDpiStringPayload& value,
    SystemVerilogDpiTransferMode expected_mode,
    std::size_t maximum_bytes = maximum_dpi_string_bytes);

struct SystemVerilogDpiChandlePayload {
    SystemVerilogDpiTransferMode mode {
        SystemVerilogDpiTransferMode::Input
    };
    SystemVerilogChandle handle { };
    bool borrowed { };
};

struct SystemVerilogDpiChandleResult {
    SystemVerilogDpiChandlePayload value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

[[nodiscard]] SystemVerilogDpiChandleResult
marshal_systemverilog_dpi_chandle(
    const SystemVerilogChandleRegistry& registry,
    SystemVerilogChandle handle,
    SystemVerilogDpiTransferMode mode);

[[nodiscard]] SystemVerilogDpiChandleResult
unmarshal_systemverilog_dpi_chandle(
    const SystemVerilogChandleRegistry& registry,
    const SystemVerilogDpiChandlePayload& value,
    SystemVerilogDpiTransferMode expected_mode);

inline constexpr std::size_t maximum_dpi_composite_elements = 65'536;
inline constexpr std::size_t maximum_dpi_composite_depth = 64;

enum class SystemVerilogDpiCompositeKind {
    Scalar,
    FixedArray,
    Struct,
    Enum,
};

struct SystemVerilogDpiTypeDescriptor {
    SystemVerilogDpiCompositeKind kind {
        SystemVerilogDpiCompositeKind::Scalar
    };
    SystemVerilogDpiScalarDomain scalar_domain {
        SystemVerilogDpiScalarDomain::Bit2
    };
    std::size_t width { };
    std::size_t element_count { };
    std::vector<SystemVerilogDpiTypeDescriptor> children;
    std::vector<std::string> member_names;
    std::vector<PackedLogic4> enum_values;

    friend bool operator==(
        const SystemVerilogDpiTypeDescriptor&,
        const SystemVerilogDpiTypeDescriptor&) = default;
};

struct SystemVerilogDpiCompositeLayout {
    std::size_t leaf_count { };
    std::size_t total_bits { };
    std::size_t payload_bytes { };
};

struct SystemVerilogDpiCompositeLayoutResult {
    SystemVerilogDpiCompositeLayout value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

struct SystemVerilogDpiCompositePayload {
    SystemVerilogDpiTransferMode mode {
        SystemVerilogDpiTransferMode::Input
    };
    SystemVerilogDpiTypeDescriptor descriptor;
    std::vector<SystemVerilogDpiScalarPayload> leaves;
};

struct SystemVerilogDpiCompositeMarshalResult {
    SystemVerilogDpiCompositePayload value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

struct SystemVerilogDpiCompositeUnmarshalResult {
    std::vector<PackedLogic4> value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

[[nodiscard]] SystemVerilogDpiCompositeLayoutResult
layout_systemverilog_dpi_composite(
    const SystemVerilogDpiTypeDescriptor& descriptor,
    std::size_t maximum_elements = maximum_dpi_composite_elements,
    std::size_t maximum_bits = maximum_dpi_scalar_bits,
    std::size_t maximum_depth = maximum_dpi_composite_depth);

[[nodiscard]] SystemVerilogDpiCompositeMarshalResult
marshal_systemverilog_dpi_composite(
    const SystemVerilogDpiTypeDescriptor& descriptor,
    const std::vector<PackedLogic4>& leaves,
    SystemVerilogDpiTransferMode mode);

[[nodiscard]] SystemVerilogDpiCompositeUnmarshalResult
unmarshal_systemverilog_dpi_composite(
    const SystemVerilogDpiCompositePayload& value,
    const SystemVerilogDpiTypeDescriptor& expected_descriptor,
    SystemVerilogDpiTransferMode expected_mode);

struct SystemVerilogDpiArrayRange {
    std::int64_t left { };
    std::int64_t right { };

    [[nodiscard]] std::int64_t low() const noexcept;
    [[nodiscard]] std::int64_t high() const noexcept;
    [[nodiscard]] int increment() const noexcept;
    [[nodiscard]] std::optional<std::size_t> size() const noexcept;

    friend bool operator==(
        const SystemVerilogDpiArrayRange&,
        const SystemVerilogDpiArrayRange&) = default;
};

struct SystemVerilogDpiOpenArrayHandle {
    std::uint32_t slot { };
    std::uint32_t epoch { };

    friend bool operator==(
        const SystemVerilogDpiOpenArrayHandle&,
        const SystemVerilogDpiOpenArrayHandle&) = default;
};

struct SystemVerilogDpiOpenArrayCreateResult {
    SystemVerilogDpiOpenArrayHandle value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

struct SystemVerilogDpiOpenArrayElementResult {
    std::vector<PackedLogic4> value;
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

struct SystemVerilogDpiOpenArrayPointerResult {
    const PackedLogic4* value { };
    std::size_t count { };
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

struct SystemVerilogDpiOpenArrayMutablePointerResult {
    PackedLogic4* value { };
    std::size_t count { };
    SystemVerilogDpiMarshallingError error {
        SystemVerilogDpiMarshallingError::None
    };
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogDpiMarshallingError::None;
    }
};

class SystemVerilogDpiOpenArrayRegistry final {
public:
    [[nodiscard]] SystemVerilogDpiOpenArrayCreateResult create(
        std::vector<SystemVerilogDpiArrayRange> ranges,
        SystemVerilogDpiTypeDescriptor element_descriptor,
        std::vector<PackedLogic4> flattened_values,
        bool contiguous,
        SystemVerilogDpiTransferMode mode);
    [[nodiscard]] bool release(
        SystemVerilogDpiOpenArrayHandle handle) noexcept;
    [[nodiscard]] bool contains(
        SystemVerilogDpiOpenArrayHandle handle) const noexcept;
    [[nodiscard]] std::optional<SystemVerilogDpiArrayRange> range(
        SystemVerilogDpiOpenArrayHandle handle,
        std::size_t dimension) const noexcept;
    [[nodiscard]] std::optional<std::size_t> dimensions(
        SystemVerilogDpiOpenArrayHandle handle) const noexcept;
    [[nodiscard]] SystemVerilogDpiOpenArrayElementResult element(
        SystemVerilogDpiOpenArrayHandle handle,
        const std::vector<std::int64_t>& indices) const;
    [[nodiscard]] SystemVerilogDpiMarshallingError store_element(
        SystemVerilogDpiOpenArrayHandle handle,
        const std::vector<std::int64_t>& indices,
        const std::vector<PackedLogic4>& value);
    [[nodiscard]] SystemVerilogDpiOpenArrayElementResult contiguous_values(
        SystemVerilogDpiOpenArrayHandle handle) const;
    [[nodiscard]] SystemVerilogDpiOpenArrayPointerResult array_pointer(
        SystemVerilogDpiOpenArrayHandle handle) const noexcept;
    [[nodiscard]] SystemVerilogDpiOpenArrayMutablePointerResult
    mutable_array_pointer(
        SystemVerilogDpiOpenArrayHandle handle) noexcept;
    [[nodiscard]] SystemVerilogDpiOpenArrayPointerResult element_pointer(
        SystemVerilogDpiOpenArrayHandle handle,
        const std::vector<std::int64_t>& indices) const noexcept;
    [[nodiscard]] SystemVerilogDpiOpenArrayMutablePointerResult
    mutable_element_pointer(
        SystemVerilogDpiOpenArrayHandle handle,
        const std::vector<std::int64_t>& indices) noexcept;

private:
    struct Slot {
        std::uint32_t epoch { 1 };
        bool live { };
        bool contiguous { };
        SystemVerilogDpiTransferMode mode {
            SystemVerilogDpiTransferMode::Input
        };
        std::vector<SystemVerilogDpiArrayRange> ranges;
        SystemVerilogDpiTypeDescriptor element_descriptor;
        std::size_t leaves_per_element { };
        std::vector<PackedLogic4> values;
    };

    [[nodiscard]] const Slot* find(
        SystemVerilogDpiOpenArrayHandle handle) const noexcept;
    [[nodiscard]] Slot* find(
        SystemVerilogDpiOpenArrayHandle handle) noexcept;
    [[nodiscard]] static std::optional<std::size_t> linear_index(
        const Slot& slot,
        const std::vector<std::int64_t>& indices) noexcept;

    std::vector<Slot> slots_;
};

} // namespace fsim::runtime
