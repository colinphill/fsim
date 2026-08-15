// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_execution.hpp"

#include "kernel_backend_execution_internal.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fsim::systemc {
namespace {

    bool report_payload_error(
        diagnostic::Engine& diagnostics, const std::string_view message)
    {
        diagnostics.error("FSIM-SC-E002", std::string { message });
        return false;
    }

    bool report_resource_error(
        diagnostic::Engine& diagnostics, const std::string_view message)
    {
        diagnostics.error("FSIM-SC-E003", std::string { message });
        return false;
    }

    bool valid_limits(const SystemCKernelExecutionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        constexpr auto maximum = static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max());
        if (limits.max_samples_per_message == 0U
            || limits.max_samples_per_message > maximum
            || limits.max_detail_bytes == 0U
            || limits.max_detail_bytes > maximum
            || limits.max_delta_cycles_per_advance == 0U
            || limits.max_advance_fs == 0U
            || limits.value_limits.max_width_bits == 0U
            || limits.value_limits.max_encoded_bytes < 56U
            || limits.value_limits.max_encoded_bytes > maximum
            || limits.value_limits.max_type_name_bytes == 0U
            || limits.value_limits.max_type_name_bytes > maximum
            || limits.value_limits.max_enum_literals == 0U
            || limits.value_limits.max_enum_literals > maximum
            || limits.value_limits.max_enum_literal_bytes == 0U
            || limits.value_limits.max_enum_literal_bytes > maximum
            || limits.value_limits.max_enum_text_bytes == 0U
            || limits.value_limits.max_enum_text_bytes > maximum) {
            return report_resource_error(diagnostics,
                "SystemC execution limits must be nonzero bounded values");
        }
        return true;
    }

    bool known_region(const SystemCAccelleraRegion region) noexcept
    {
        using enum SystemCAccelleraRegion;
        return region == evaluate || region == update || region == notification
            || region == quiescent || region == terminal;
    }

    bool known_advance_kind(const SystemCKernelAdvanceKind kind) noexcept
    {
        return kind == SystemCKernelAdvanceKind::delta
            || kind == SystemCKernelAdvanceKind::time;
    }

    bool known_execution_status(
        const SystemCKernelExecutionStatus status) noexcept
    {
        using enum SystemCKernelExecutionStatus;
        return status == quiescent || status == running || status == paused
            || status == stopped || status == error || status == terminal;
    }

    bool known_execution_code(const SystemCKernelExecutionCode code) noexcept
    {
        using enum SystemCKernelExecutionCode;
        return code == none || code == state || code == payload
            || code == resource || code == upstream;
    }

    bool known_session_state(const SystemCKernelSessionState state) noexcept
    {
        using enum SystemCKernelSessionState;
        return state == vacant || state == constructing || state == elaborated
            || state == quiescent || state == terminal || state == failed;
    }

    bool valid_scalar(const SystemCKernelScalarValue& value,
        const SystemCKernelExecutionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (value.typed) {
            if (value.bits != 0U || value.width != 0U || value.is_signed) {
                return report_payload_error(diagnostics,
                    "SystemC typed value has a noncanonical legacy projection");
            }
            diagnostic::Engine value_diagnostics;
            if (!validate_systemc_kernel_value(
                    *value.typed, limits.value_limits, value_diagnostics)) {
                const auto resource = std::ranges::any_of(
                    value_diagnostics.diagnostics(), [](const auto& item) {
                        return item.code == "FSIM-SC-V003";
                    });
                return resource
                    ? report_resource_error(diagnostics,
                          "SystemC typed value exceeds its governed limits")
                    : report_payload_error(diagnostics,
                          "SystemC typed value metadata or planes are invalid");
            }
            return true;
        }
        if (value.width == 0U || value.width > 64U
            || (value.width < 64U && (value.bits >> value.width) != 0U)) {
            return report_payload_error(diagnostics,
                "SystemC scalar value has a noncanonical width or bit pattern");
        }
        return true;
    }

    bool valid_order(const SystemCKernelExecutionOrder& order,
        diagnostic::Engine& diagnostics)
    {
        if (!known_region(order.region) || !order.island.valid()
            || !order.sequence.valid()) {
            return report_payload_error(diagnostics,
                "SystemC execution order has an invalid region, island, or sequence");
        }
        return true;
    }

    class ByteWriter final {
    public:
        void append_u8(const std::uint8_t value)
        {
            bytes_.push_back(static_cast<std::byte>(value));
        }

        void append_u16(const std::uint16_t value) { append_integral(value); }
        void append_u32(const std::uint32_t value) { append_integral(value); }
        void append_u64(const std::uint64_t value) { append_integral(value); }

        void append_id(const SystemCIslandId value)
        {
            append_u64(value.high);
            append_u64(value.low);
        }

        void append_id(const SystemCEndpointId value)
        {
            append_u64(value.high);
            append_u64(value.low);
        }

        void append_text(const std::string_view value)
        {
            const auto* begin = reinterpret_cast<const std::byte*>(value.data());
            bytes_.insert(bytes_.end(), begin, begin + value.size());
        }

        void append_bytes(const std::span<const std::byte> value)
        {
            bytes_.insert(bytes_.end(), value.begin(), value.end());
        }

        [[nodiscard]] std::vector<std::byte> take() { return std::move(bytes_); }

    private:
        template <typename T>
        void append_integral(const T value)
        {
            using unsigned_type = std::make_unsigned_t<T>;
            auto remaining = static_cast<unsigned_type>(value);
            for (std::size_t index = 0U; index < sizeof(T); ++index) {
                append_u8(static_cast<std::uint8_t>(remaining & 0xffU));
                remaining >>= 8U;
            }
        }

        std::vector<std::byte> bytes_;
    };

    class ByteReader final {
    public:
        explicit ByteReader(const std::span<const std::byte> bytes)
            : bytes_ { bytes }
        {
        }

        [[nodiscard]] bool read_u8(std::uint8_t& value)
        {
            return read_integral(value);
        }
        [[nodiscard]] bool read_u16(std::uint16_t& value)
        {
            return read_integral(value);
        }
        [[nodiscard]] bool read_u32(std::uint32_t& value)
        {
            return read_integral(value);
        }
        [[nodiscard]] bool read_u64(std::uint64_t& value)
        {
            return read_integral(value);
        }

        [[nodiscard]] bool read_id(SystemCIslandId& value)
        {
            return read_u64(value.high) && read_u64(value.low);
        }

        [[nodiscard]] bool read_id(SystemCEndpointId& value)
        {
            return read_u64(value.high) && read_u64(value.low);
        }

        [[nodiscard]] bool read_text(
            const std::size_t size, std::string& value)
        {
            if (size > remaining()) {
                return false;
            }
            value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_),
                size);
            offset_ += size;
            return true;
        }

        [[nodiscard]] bool read_bytes(
            const std::size_t size, std::vector<std::byte>& value)
        {
            if (size > remaining()) {
                return false;
            }
            const auto begin = bytes_.begin()
                + static_cast<std::ptrdiff_t>(offset_);
            value.assign(begin,
                begin + static_cast<std::ptrdiff_t>(size));
            offset_ += size;
            return true;
        }

        [[nodiscard]] bool done() const noexcept
        {
            return offset_ == bytes_.size();
        }

    private:
        template <typename T>
        [[nodiscard]] bool read_integral(T& value)
        {
            if (sizeof(T) > remaining()) {
                return false;
            }
            using unsigned_type = std::make_unsigned_t<T>;
            std::uint64_t result { };
            for (std::size_t index = 0U; index < sizeof(T); ++index) {
                result |= static_cast<std::uint64_t>(
                              std::to_integer<std::uint8_t>(bytes_[offset_++]))
                    << (index * 8U);
            }
            value = static_cast<T>(static_cast<unsigned_type>(result));
            return true;
        }

        [[nodiscard]] std::size_t remaining() const noexcept
        {
            return bytes_.size() - offset_;
        }

        std::span<const std::byte> bytes_;
        std::size_t offset_ { };
    };

    void append_order(ByteWriter& writer,
        const SystemCKernelExecutionOrder& order)
    {
        writer.append_u64(order.time_fs);
        writer.append_u64(order.delta);
        writer.append_u8(static_cast<std::uint8_t>(order.region));
        for (std::size_t index = 0U; index < 7U; ++index) {
            writer.append_u8(0U);
        }
        writer.append_id(order.island);
        writer.append_u64(order.sequence.value);
    }

    bool read_order(ByteReader& reader, SystemCKernelExecutionOrder& order)
    {
        std::uint8_t region { };
        std::uint8_t reserved { };
        if (!reader.read_u64(order.time_fs) || !reader.read_u64(order.delta)
            || !reader.read_u8(region)) {
            return false;
        }
        for (std::size_t index = 0U; index < 7U; ++index) {
            if (!reader.read_u8(reserved) || reserved != 0U) {
                return false;
            }
        }
        order.region = static_cast<SystemCAccelleraRegion>(region);
        return reader.read_id(order.island)
            && reader.read_u64(order.sequence.value);
    }

    bool append_scalar(ByteWriter& writer,
        const SystemCKernelScalarValue& value,
        const SystemCKernelExecutionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        writer.append_u8(value.typed ? 1U : 0U);
        writer.append_u8(0U);
        writer.append_u16(0U);
        if (value.typed) {
            const auto encoded = serialize_systemc_kernel_value(
                *value.typed, limits.value_limits, diagnostics);
            if (!encoded) {
                return false;
            }
            writer.append_u32(static_cast<std::uint32_t>(encoded->size()));
            writer.append_bytes(*encoded);
            return true;
        }
        writer.append_u64(value.bits);
        writer.append_u16(value.width);
        writer.append_u8(value.is_signed ? 1U : 0U);
        writer.append_u8(0U);
        return true;
    }

    bool read_scalar(ByteReader& reader, SystemCKernelScalarValue& value,
        const SystemCKernelExecutionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        std::uint8_t representation { };
        std::uint8_t representation_reserved8 { };
        std::uint16_t representation_reserved16 { };
        if (!reader.read_u8(representation)
            || !reader.read_u8(representation_reserved8)
            || !reader.read_u16(representation_reserved16)
            || representation > 1U || representation_reserved8 != 0U
            || representation_reserved16 != 0U) {
            return false;
        }
        if (representation == 1U) {
            std::uint32_t encoded_size { };
            std::vector<std::byte> encoded;
            if (!reader.read_u32(encoded_size)
                || encoded_size > limits.value_limits.max_encoded_bytes
                || !reader.read_bytes(encoded_size, encoded)) {
                return false;
            }
            auto typed = deserialize_systemc_kernel_value(
                encoded, limits.value_limits, diagnostics);
            if (!typed) {
                return false;
            }
            value.typed = std::move(*typed);
            return true;
        }
        std::uint8_t is_signed { };
        std::uint8_t reserved { };
        if (!reader.read_u64(value.bits) || !reader.read_u16(value.width)
            || !reader.read_u8(is_signed) || !reader.read_u8(reserved)
            || is_signed > 1U || reserved != 0U) {
            return false;
        }
        value.is_signed = is_signed != 0U;
        return true;
    }

    bool valid_receipt(const SystemCKernelExecutionReceipt& receipt,
        const SystemCKernelExecutionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (!valid_limits(limits, diagnostics)) {
            return false;
        }
        if (!known_session_state(receipt.session_state)
            || !known_execution_status(receipt.status)
            || !known_execution_code(receipt.code)
            || !valid_order(receipt.order, diagnostics)
            || receipt.detail.size() > limits.max_detail_bytes
            || receipt.detail.find('\0') != std::string::npos) {
            return report_payload_error(diagnostics,
                "SystemC execution receipt has invalid state, order, or detail");
        }
        if ((receipt.current_activity || receipt.future_activity)
                != receipt.next_activity_time_fs.has_value()
            || (receipt.next_activity_time_fs
                && *receipt.next_activity_time_fs < receipt.order.time_fs)) {
            return report_payload_error(diagnostics,
                "SystemC execution next-activity state is inconsistent");
        }
        if (receipt.samples.size() > limits.max_samples_per_message) {
            return report_resource_error(diagnostics,
                "SystemC execution sample count exceeds its governed limit");
        }
        std::set<SystemCEndpointId> endpoints;
        std::optional<SystemCKernelExecutionOrder> previous;
        for (const auto& sample : receipt.samples) {
            if (!sample.endpoint.valid()
                || !valid_order(sample.order, diagnostics)
                || sample.order.island != receipt.order.island
                || !valid_scalar(sample.value, limits, diagnostics)
                || (previous && !(*previous < sample.order))
                || !endpoints.insert(sample.endpoint).second) {
                return report_payload_error(diagnostics,
                    "SystemC execution samples are invalid, repeated, or unordered");
            }
            previous = sample.order;
        }
        return true;
    }

} // namespace

std::optional<std::vector<std::byte>> serialize_systemc_apply_inputs_payload(
    const SystemCKernelApplyInputsPayload& payload,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)
        || !valid_scalar(payload.value, limits, diagnostics)) {
        return std::nullopt;
    }
    ByteWriter writer;
    writer.append_u32(kSystemCKernelExecutionPayloadVersion);
    if (!append_scalar(writer, payload.value, limits, diagnostics)) {
        return std::nullopt;
    }
    return writer.take();
}

std::optional<SystemCKernelApplyInputsPayload>
deserialize_systemc_apply_inputs_payload(const std::span<const std::byte> bytes,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return std::nullopt;
    }
    ByteReader reader { bytes };
    std::uint32_t schema { };
    SystemCKernelApplyInputsPayload payload;
    if (!reader.read_u32(schema)
        || schema != kSystemCKernelExecutionPayloadVersion
        || !read_scalar(reader, payload.value, limits, diagnostics)
        || !reader.done()
        || !valid_scalar(payload.value, limits, diagnostics)) {
        report_payload_error(diagnostics,
            "SystemC input payload is malformed, truncated, or trailing");
        return std::nullopt;
    }
    return payload;
}

std::optional<std::vector<std::byte>> serialize_systemc_advance_payload(
    const SystemCKernelAdvancePayload& payload,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)
        || !known_advance_kind(payload.kind)
        || (payload.kind == SystemCKernelAdvanceKind::delta
            && payload.duration_fs != 0U)
        || (payload.kind == SystemCKernelAdvanceKind::time
            && (payload.duration_fs == 0U
                || payload.duration_fs > limits.max_advance_fs))) {
        report_payload_error(diagnostics,
            "SystemC advance payload has an invalid kind or duration");
        return std::nullopt;
    }
    ByteWriter writer;
    writer.append_u32(kSystemCKernelExecutionPayloadVersion);
    writer.append_u8(static_cast<std::uint8_t>(payload.kind));
    writer.append_u8(0U);
    writer.append_u16(0U);
    writer.append_u64(payload.duration_fs);
    return writer.take();
}

std::optional<SystemCKernelAdvancePayload> deserialize_systemc_advance_payload(
    const std::span<const std::byte> bytes,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return std::nullopt;
    }
    ByteReader reader { bytes };
    std::uint32_t schema { };
    std::uint8_t kind { };
    std::uint8_t reserved8 { };
    std::uint16_t reserved16 { };
    SystemCKernelAdvancePayload payload;
    if (!reader.read_u32(schema) || !reader.read_u8(kind)
        || !reader.read_u8(reserved8) || !reader.read_u16(reserved16)
        || !reader.read_u64(payload.duration_fs) || !reader.done()
        || schema != kSystemCKernelExecutionPayloadVersion
        || reserved8 != 0U || reserved16 != 0U) {
        report_payload_error(diagnostics,
            "SystemC advance payload is malformed, truncated, or trailing");
        return std::nullopt;
    }
    payload.kind = static_cast<SystemCKernelAdvanceKind>(kind);
    if (!known_advance_kind(payload.kind)
        || (payload.kind == SystemCKernelAdvanceKind::delta
            && payload.duration_fs != 0U)
        || (payload.kind == SystemCKernelAdvanceKind::time
            && (payload.duration_fs == 0U
                || payload.duration_fs > limits.max_advance_fs))) {
        report_payload_error(diagnostics,
            "SystemC advance payload has an invalid kind or duration");
        return std::nullopt;
    }
    return payload;
}

std::optional<std::vector<std::byte>> serialize_systemc_execution_receipt(
    const SystemCKernelExecutionReceipt& receipt,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_receipt(receipt, limits, diagnostics)) {
        return std::nullopt;
    }
    ByteWriter writer;
    writer.append_u32(kSystemCKernelExecutionPayloadVersion);
    writer.append_u8(static_cast<std::uint8_t>(receipt.session_state));
    writer.append_u8(static_cast<std::uint8_t>(receipt.status));
    writer.append_u8(receipt.published ? 1U : 0U);
    writer.append_u8(receipt.current_activity ? 1U : 0U);
    writer.append_u8(receipt.future_activity ? 1U : 0U);
    writer.append_u8(receipt.next_activity_time_fs ? 1U : 0U);
    writer.append_u16(static_cast<std::uint16_t>(receipt.code));
    append_order(writer, receipt.order);
    writer.append_u64(receipt.next_activity_time_fs.value_or(0U));
    writer.append_u32(static_cast<std::uint32_t>(receipt.samples.size()));
    writer.append_u32(static_cast<std::uint32_t>(receipt.detail.size()));
    for (const auto& sample : receipt.samples) {
        writer.append_id(sample.endpoint);
        append_order(writer, sample.order);
        if (!append_scalar(writer, sample.value, limits, diagnostics)) {
            return std::nullopt;
        }
        writer.append_u8(sample.dirty ? 1U : 0U);
        writer.append_u8(0U);
        writer.append_u16(0U);
    }
    writer.append_text(receipt.detail);
    return writer.take();
}

std::optional<SystemCKernelExecutionReceipt>
deserialize_systemc_execution_receipt(const std::span<const std::byte> bytes,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return std::nullopt;
    }
    ByteReader reader { bytes };
    std::uint32_t schema { };
    std::uint8_t state { };
    std::uint8_t status { };
    std::uint8_t published { };
    std::uint8_t current { };
    std::uint8_t future { };
    std::uint8_t has_next { };
    std::uint16_t code { };
    std::uint64_t next { };
    std::uint32_t sample_count { };
    std::uint32_t detail_size { };
    SystemCKernelExecutionReceipt receipt;
    if (!reader.read_u32(schema) || !reader.read_u8(state)
        || !reader.read_u8(status) || !reader.read_u8(published)
        || !reader.read_u8(current) || !reader.read_u8(future)
        || !reader.read_u8(has_next) || !reader.read_u16(code)
        || !read_order(reader, receipt.order) || !reader.read_u64(next)
        || !reader.read_u32(sample_count) || !reader.read_u32(detail_size)
        || schema != kSystemCKernelExecutionPayloadVersion
        || published > 1U || current > 1U || future > 1U || has_next > 1U
        || sample_count > limits.max_samples_per_message
        || detail_size > limits.max_detail_bytes) {
        report_payload_error(diagnostics,
            "SystemC execution receipt header is malformed or oversized");
        return std::nullopt;
    }
    receipt.session_state = static_cast<SystemCKernelSessionState>(state);
    receipt.status = static_cast<SystemCKernelExecutionStatus>(status);
    receipt.code = static_cast<SystemCKernelExecutionCode>(code);
    receipt.published = published != 0U;
    receipt.current_activity = current != 0U;
    receipt.future_activity = future != 0U;
    if (has_next != 0U) {
        receipt.next_activity_time_fs = next;
    } else if (next != 0U) {
        report_payload_error(diagnostics,
            "SystemC execution receipt has a noncanonical absent activity time");
        return std::nullopt;
    }
    receipt.samples.reserve(sample_count);
    for (std::uint32_t index = 0U; index < sample_count; ++index) {
        SystemCKernelExecutionSample sample;
        std::uint8_t dirty { };
        std::uint8_t reserved8 { };
        std::uint16_t reserved16 { };
        if (!reader.read_id(sample.endpoint)
            || !read_order(reader, sample.order)
            || !read_scalar(reader, sample.value, limits, diagnostics)
            || !reader.read_u8(dirty)
            || !reader.read_u8(reserved8) || !reader.read_u16(reserved16)
            || dirty > 1U || reserved8 != 0U || reserved16 != 0U) {
            report_payload_error(diagnostics,
                "SystemC execution sample is malformed or truncated");
            return std::nullopt;
        }
        sample.dirty = dirty != 0U;
        receipt.samples.push_back(sample);
    }
    if (!reader.read_text(detail_size, receipt.detail) || !reader.done()
        || !valid_receipt(receipt, limits, diagnostics)) {
        report_payload_error(diagnostics,
            "SystemC execution receipt is malformed, trailing, or inconsistent");
        return std::nullopt;
    }
    return receipt;
}

const char* systemc_kernel_execution_diagnostic_code(
    const SystemCKernelExecutionCode code) noexcept
{
    switch (code) {
    case SystemCKernelExecutionCode::none:
        return "";
    case SystemCKernelExecutionCode::state:
        return "FSIM-SC-E001";
    case SystemCKernelExecutionCode::payload:
        return "FSIM-SC-E002";
    case SystemCKernelExecutionCode::resource:
        return "FSIM-SC-E003";
    case SystemCKernelExecutionCode::upstream:
        return "FSIM-SC-E004";
    }
    return "FSIM-SC-E001";
}

namespace detail {

    bool systemc_kernel_execution_limits_valid(
        const SystemCKernelExecutionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        return valid_limits(limits, diagnostics);
    }

    bool systemc_kernel_apply_scalar(backend_scalar_endpoint& endpoint,
        const SystemCKernelScalarValue& value, std::string& error) noexcept
    {
        try {
            if (endpoint.backend_direction() != backend_endpoint_direction::input) {
                error = "SystemC input application targeted a non-input endpoint";
                return false;
            }
            if (!endpoint.apply_backend_scalar(
                    value.bits, value.width, value.is_signed)) {
                error = "SystemC input value does not match the bound scalar endpoint";
                return false;
            }
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC scalar-input failure";
        }
        return false;
    }

    std::optional<SystemCKernelScalarValue> systemc_kernel_sample_scalar(
        const backend_scalar_endpoint& endpoint, std::string& error) noexcept
    {
        try {
            SystemCKernelScalarValue value;
            value.width = endpoint.backend_width();
            value.is_signed = endpoint.backend_signed();
            if (!endpoint.sample_backend_scalar(value.bits)) {
                error = "SystemC scalar endpoint cannot be sampled";
                return std::nullopt;
            }
            return value;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC scalar-sampling failure";
        }
        return std::nullopt;
    }

    bool systemc_kernel_apply_value(backend_value_endpoint& endpoint,
        const SystemCKernelValue& value, std::string& error) noexcept
    {
        try {
            if (endpoint.backend_direction()
                != backend_endpoint_direction::input) {
                error = "SystemC typed input application targeted a non-input endpoint";
                return false;
            }
            if (!endpoint.apply_backend_value(value)) {
                error = "SystemC typed input does not match the bound value endpoint";
                return false;
            }
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC typed-input failure";
        }
        return false;
    }

    std::optional<SystemCKernelValue> systemc_kernel_sample_value(
        const backend_value_endpoint& endpoint, std::string& error) noexcept
    {
        try {
            auto value = endpoint.sample_backend_value();
            if (!value) {
                error = "SystemC value endpoint cannot be sampled";
                return std::nullopt;
            }
            return value;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC value-sampling failure";
        }
        return std::nullopt;
    }

    namespace {

        std::optional<std::uint64_t> to_femtoseconds(const std::uint64_t ticks,
            const std::uint64_t resolution, std::string& error)
        {
            if (resolution == 0U
                || ticks > std::numeric_limits<std::uint64_t>::max() / resolution) {
                error = "upstream SystemC time exceeds the execution protocol range";
                return std::nullopt;
            }
            return ticks * resolution;
        }

        SystemCKernelExecutionStatus native_status(
            const sc_core::sc_simcontext& context)
        {
            if (context.sim_status() == sc_core::SC_SIM_ERROR) {
                return SystemCKernelExecutionStatus::error;
            }
            const auto status = context.get_status();
            if (context.sim_status() == sc_core::SC_SIM_USER_STOP
                || status == sc_core::SC_STOPPED
                || status == sc_core::SC_END_OF_SIMULATION) {
                return SystemCKernelExecutionStatus::stopped;
            }
            if (status == sc_core::SC_RUNNING) {
                return SystemCKernelExecutionStatus::running;
            }
            if (status == sc_core::SC_SUSPENDED
                || (status == sc_core::SC_PAUSED
                    && context.pending_activity_at_current_time())) {
                return SystemCKernelExecutionStatus::paused;
            }
            return SystemCKernelExecutionStatus::quiescent;
        }

        bool terminal_status(const SystemCKernelExecutionStatus status) noexcept
        {
            return status == SystemCKernelExecutionStatus::stopped
                || status == SystemCKernelExecutionStatus::error;
        }

        bool observe_next_activity(sc_core::sc_simcontext& context,
            const std::uint64_t time_resolution_fs,
            SystemCKernelNativeObservation& result, std::string& error)
        {
            if (result.current_activity) {
                result.next_activity_time_fs = result.time_fs;
                return true;
            }
            if (!result.future_activity) {
                return true;
            }
            const auto distance = sc_core::sc_time_to_pending_activity(&context);
            if (distance.value() > std::numeric_limits<std::uint64_t>::max()
                    - context.time_stamp().value()) {
                error = "next upstream SystemC activity exceeds the protocol range";
                return false;
            }
            result.next_activity_time_fs = to_femtoseconds(
                context.time_stamp().value() + distance.value(),
                time_resolution_fs, error);
            return result.next_activity_time_fs.has_value();
        }

    } // namespace

    std::optional<SystemCKernelNativeObservation> systemc_kernel_observe_native(
        sc_core::sc_simcontext& context, const std::uint64_t time_resolution_fs,
        std::string& error) noexcept
    {
        try {
            SystemCKernelNativeObservation result;
            result.status = native_status(context);
            const auto time = to_femtoseconds(
                context.time_stamp().value(), time_resolution_fs, error);
            if (!time) {
                return std::nullopt;
            }
            result.time_fs = *time;
            result.delta = sc_core::sc_delta_count();
            result.current_activity = context.pending_activity_at_current_time();
            result.future_activity = sc_core::sc_pending_activity_at_future_time(
                &context);
            if (!observe_next_activity(
                    context, time_resolution_fs, result, error)) {
                return std::nullopt;
            }
            return result;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC observation failure";
        }
        return std::nullopt;
    }

    namespace {

        std::optional<SystemCKernelNativeObservation> advance_delta(
            sc_core::sc_simcontext& context,
            SystemCKernelNativeObservation observation,
            const SystemCKernelExecutionLimits& limits,
            const std::uint64_t time_resolution_fs, std::string& error)
        {
            std::uint64_t cycles { };
            while (observation.current_activity
                && cycles < limits.max_delta_cycles_per_advance) {
                sc_core::sc_start(sc_core::SC_ZERO_TIME,
                    sc_core::SC_EXIT_ON_STARVATION);
                ++cycles;
                auto next = systemc_kernel_observe_native(
                    context, time_resolution_fs, error);
                if (!next) {
                    return std::nullopt;
                }
                observation = *next;
                if (terminal_status(observation.status)) {
                    break;
                }
            }
            if (observation.current_activity
                && !terminal_status(observation.status)) {
                error = "SystemC delta advancement exhausted its governed cycle limit";
                return std::nullopt;
            }
            return observation;
        }

        std::optional<SystemCKernelNativeObservation> advance_time(
            sc_core::sc_simcontext& context,
            const SystemCKernelNativeObservation& before,
            const SystemCKernelAdvancePayload& payload,
            const SystemCKernelExecutionLimits& limits,
            const std::uint64_t time_resolution_fs, std::string& error)
        {
            if (payload.duration_fs == 0U
                || payload.duration_fs > limits.max_advance_fs
                || time_resolution_fs == 0U
                || payload.duration_fs % time_resolution_fs != 0U) {
                error = "time advancement is not exact at the session resolution";
                return std::nullopt;
            }
            if (before.time_fs > std::numeric_limits<std::uint64_t>::max()
                    - payload.duration_fs) {
                error = "requested SystemC advancement exceeds the protocol range";
                return std::nullopt;
            }
            const auto duration = sc_core::sc_time::from_value(
                payload.duration_fs / time_resolution_fs);
            sc_core::sc_start(duration, sc_core::SC_RUN_TO_TIME);
            auto after = systemc_kernel_observe_native(
                context, time_resolution_fs, error);
            if (!after) {
                return std::nullopt;
            }
            const auto target_time_fs = before.time_fs + payload.duration_fs;
            if (after->time_fs < target_time_fs
                && !terminal_status(after->status)) {
                after->status = SystemCKernelExecutionStatus::paused;
            }
            return after;
        }

    } // namespace

    std::optional<SystemCKernelNativeObservation> systemc_kernel_advance_native(
        sc_core::sc_simcontext& context,
        const SystemCKernelAdvancePayload& payload,
        const SystemCKernelExecutionLimits& limits,
        const std::uint64_t time_resolution_fs, std::string& error) noexcept
    {
        try {
            auto before = systemc_kernel_observe_native(
                context, time_resolution_fs, error);
            if (!before) {
                return std::nullopt;
            }
            if (terminal_status(before->status)) {
                error = "upstream SystemC execution is already terminal";
                return std::nullopt;
            }
            if (payload.kind == SystemCKernelAdvanceKind::delta) {
                if (payload.duration_fs != 0U) {
                    error = "delta advancement requires a zero duration";
                    return std::nullopt;
                }
                return advance_delta(context, *before, limits,
                    time_resolution_fs, error);
            }
            if (payload.kind == SystemCKernelAdvanceKind::time) {
                return advance_time(context, *before, payload, limits,
                    time_resolution_fs, error);
            }
            error = "SystemC advancement has an unknown operation kind";
            return std::nullopt;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC advancement failure";
        }
        return std::nullopt;
    }

} // namespace detail
} // namespace fsim::systemc
