// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_observation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>

namespace fsim::systemc {
namespace {

    constexpr std::array<std::byte, 4> kMagic { std::byte { 'F' },
        std::byte { 'S' }, std::byte { 'C' }, std::byte { 'O' } };
    constexpr std::size_t kHeaderBytes = 32U;
    constexpr std::size_t kRecordBytes = 108U;

    bool report_error(diagnostic::Engine& diagnostics,
        const SystemCKernelObservationCode code, const std::string_view message)
    {
        diagnostics.error(systemc_kernel_observation_diagnostic_code(code),
            std::string { message });
        return false;
    }

    bool known_kind(const SystemCKernelObservationKind kind) noexcept
    {
        const auto raw = static_cast<std::uint8_t>(kind);
        return raw >= static_cast<std::uint8_t>(
                   SystemCKernelObservationKind::debugger_read)
            && raw <= static_cast<std::uint8_t>(
                   SystemCKernelObservationKind::tlm2_end);
    }

    bool known_region(const SystemCAccelleraRegion region) noexcept
    {
        const auto raw = static_cast<std::uint8_t>(region);
        return raw
            >= static_cast<std::uint8_t>(SystemCAccelleraRegion::evaluate)
            && raw
            <= static_cast<std::uint8_t>(SystemCAccelleraRegion::terminal);
    }

    bool transaction_kind(const SystemCKernelObservationKind kind) noexcept
    {
        return kind >= SystemCKernelObservationKind::tlm1_begin;
    }

    bool safe_point_kind(const SystemCKernelObservationKind kind) noexcept
    {
        return kind <= SystemCKernelObservationKind::report;
    }

    bool valid_transaction_record(
        const SystemCKernelObservationRecord& record,
        const SystemCKernelObservationLimits& limits)
    {
        diagnostic::Engine nested_diagnostics;
        if (record.kind <= SystemCKernelObservationKind::tlm1_end) {
            const auto value = deserialize_systemc_kernel_tlm1_transaction(
                record.transaction_bytes, limits.tlm1_limits,
                nested_diagnostics);
            if (!value || value->transaction != record.transaction
                || value->endpoint != record.endpoint
                || value->peer != record.peer
                || value->sequence != record.order.sequence
                || value->time_fs != record.order.time_fs
                || value->delta != record.order.delta) {
                return false;
            }
            return (record.kind == SystemCKernelObservationKind::tlm1_begin
                       && value->state == SystemCKernelTlm1State::begun)
                || (record.kind == SystemCKernelObservationKind::tlm1_update
                    && (value->state == SystemCKernelTlm1State::blocked
                        || value->state
                            == SystemCKernelTlm1State::unavailable))
                || (record.kind == SystemCKernelObservationKind::tlm1_end
                    && (value->state == SystemCKernelTlm1State::completed
                        || value->state == SystemCKernelTlm1State::canceled));
        }
        const auto value = deserialize_systemc_kernel_tlm2_transaction(
            record.transaction_bytes, limits.tlm2_limits, nested_diagnostics);
        if (!value || value->transaction != record.transaction
            || value->endpoint != record.endpoint || value->peer != record.peer
            || value->sequence != record.order.sequence
            || value->time_fs != record.order.time_fs
            || value->delta != record.order.delta) {
            return false;
        }
        return (record.kind == SystemCKernelObservationKind::tlm2_begin
                   && value->state == SystemCKernelTlm2State::begun)
            || (record.kind == SystemCKernelObservationKind::tlm2_phase
                && value->operation
                    >= SystemCKernelTlm2Operation::nonblocking_forward
                && value->operation
                    <= SystemCKernelTlm2Operation::nonblocking_backward)
            || (record.kind == SystemCKernelObservationKind::tlm2_dmi
                && (value->dmi
                    || value->operation
                        == SystemCKernelTlm2Operation::invalidate_direct_memory))
            || (record.kind == SystemCKernelObservationKind::tlm2_debug
                && value->operation
                    == SystemCKernelTlm2Operation::debug_transport)
            || (record.kind == SystemCKernelObservationKind::tlm2_end
                && value->state >= SystemCKernelTlm2State::completed);
    }

    bool valid_limits(const SystemCKernelObservationLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_adapters == 0U || limits.max_records == 0U
            || limits.max_detail_bytes == 0U
            || limits.max_transaction_bytes == 0U
            || limits.max_encoded_bytes < kHeaderBytes) {
            return report_error(diagnostics,
                SystemCKernelObservationCode::resource,
                "SystemC observation limits are invalid");
        }
        return true;
    }

    bool valid_safe_point(const SystemCKernelExecutionOrder& order,
        const SystemCIslandId island, diagnostic::Engine& diagnostics)
    {
        if (order.region != SystemCAccelleraRegion::quiescent
            || order.island != island || !order.sequence.valid()) {
            return report_error(diagnostics,
                SystemCKernelObservationCode::safe_point,
                "SystemC debugger access is permitted only at an island safe point");
        }
        return true;
    }

    class Writer {
    public:
        explicit Writer(const std::size_t reserve) { bytes_.reserve(reserve); }

        void u8(const std::uint8_t value) { bytes_.push_back(std::byte { value }); }
        void u32(const std::uint32_t value)
        {
            for (unsigned shift = 0U; shift < 32U; shift += 8U) {
                u8(static_cast<std::uint8_t>(value >> shift));
            }
        }
        void u64(const std::uint64_t value)
        {
            for (unsigned shift = 0U; shift < 64U; shift += 8U) {
                u8(static_cast<std::uint8_t>(value >> shift));
            }
        }
        template <typename Domain>
        void id(const SystemCBackendId<Domain> value)
        {
            u64(value.high);
            u64(value.low);
        }
        void raw(const std::span<const std::byte> bytes)
        {
            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        }
        void text(const std::string_view value)
        {
            raw(std::as_bytes(std::span { value.data(), value.size() }));
        }
        [[nodiscard]] std::vector<std::byte> take() &&
        {
            return std::move(bytes_);
        }

    private:
        std::vector<std::byte> bytes_;
    };

    class Reader {
    public:
        explicit Reader(const std::span<const std::byte> bytes)
            : bytes_ { bytes }
        {
        }

        bool u8(std::uint8_t& value)
        {
            if (offset_ >= bytes_.size()) {
                return false;
            }
            value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
            return true;
        }
        bool u32(std::uint32_t& value)
        {
            value = 0U;
            for (unsigned shift = 0U; shift < 32U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte)) {
                    return false;
                }
                value |= static_cast<std::uint32_t>(byte) << shift;
            }
            return true;
        }
        bool u64(std::uint64_t& value)
        {
            value = 0U;
            for (unsigned shift = 0U; shift < 64U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte)) {
                    return false;
                }
                value |= static_cast<std::uint64_t>(byte) << shift;
            }
            return true;
        }
        template <typename Domain>
        bool id(SystemCBackendId<Domain>& value)
        {
            return u64(value.high) && u64(value.low);
        }
        bool raw(const std::size_t size, std::span<const std::byte>& value)
        {
            if (size > remaining()) {
                return false;
            }
            value = bytes_.subspan(offset_, size);
            offset_ += size;
            return true;
        }
        bool text(const std::size_t size, std::string& value)
        {
            std::span<const std::byte> bytes;
            if (!raw(size, bytes)) {
                return false;
            }
            value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        }
        [[nodiscard]] std::size_t remaining() const noexcept
        {
            return bytes_.size() - offset_;
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t offset_ { };
    };

    bool checked_add(std::size_t& total, const std::size_t amount) noexcept
    {
        if (amount > std::numeric_limits<std::size_t>::max() - total) {
            return false;
        }
        total += amount;
        return true;
    }

    bool valid_record(const SystemCKernelObservationRecord& record,
        const SystemCIslandId island, const SystemCKernelObservationLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (!known_kind(record.kind) || !known_region(record.order.region)
            || record.order.island != island
            || !record.order.sequence.valid()
            || record.detail.size() > limits.max_detail_bytes
            || record.transaction_bytes.size() > limits.max_transaction_bytes) {
            return report_error(diagnostics,
                SystemCKernelObservationCode::metadata,
                "SystemC observation record metadata is invalid");
        }
        if (safe_point_kind(record.kind)
            && record.order.region != SystemCAccelleraRegion::quiescent) {
            return report_error(diagnostics,
                SystemCKernelObservationCode::safe_point,
                "SystemC debugger observation was not captured at a safe point");
        }
        if (transaction_kind(record.kind)) {
            if (!record.endpoint.valid() || !record.peer.valid()
                || !record.transaction.valid() || record.transaction_bytes.empty()
                || record.value) {
                return report_error(diagnostics,
                    SystemCKernelObservationCode::metadata,
                    "SystemC TLM observation lacks transaction correlation");
            }
            if (!valid_transaction_record(record, limits)) {
                return report_error(diagnostics,
                    SystemCKernelObservationCode::metadata,
                    "SystemC TLM observation payload and correlation disagree");
            }
        } else if (!record.transaction_bytes.empty()
            || record.transaction.valid() || record.peer.valid()) {
            return report_error(diagnostics,
                SystemCKernelObservationCode::metadata,
                "SystemC debugger observation contains transaction state");
        }
        const bool debugger_value
            = record.kind == SystemCKernelObservationKind::debugger_read
            || record.kind == SystemCKernelObservationKind::debugger_write;
        if (debugger_value != (record.endpoint.valid() && record.value.has_value())) {
            return report_error(diagnostics,
                SystemCKernelObservationCode::metadata,
                "SystemC debugger value observation is incomplete");
        }
        if (!transaction_kind(record.kind) && !debugger_value
            && (record.endpoint.valid() || record.value)) {
            return report_error(diagnostics,
                SystemCKernelObservationCode::metadata,
                "SystemC inventory or report observation contains channel state");
        }
        if (record.value
            && !validate_systemc_kernel_value(
                *record.value, limits.value_limits, diagnostics)) {
            return false;
        }
        return true;
    }

    const SystemCKernelChannelInventoryEntry* find_channel(
        const SystemCKernelChannelInventorySnapshot& inventory,
        const SystemCEndpointId endpoint)
    {
        const auto found = std::ranges::find(
            inventory.channels, endpoint, &SystemCKernelChannelInventoryEntry::channel);
        return found == inventory.channels.end() ? nullptr : &*found;
    }

} // namespace

SystemCKernelSafePointObserver::SystemCKernelSafePointObserver(
    SystemCKernelChannelInventorySnapshot inventory,
    SystemCKernelObservationLimits limits)
    : limits_ { std::move(limits) }
    , inventory_ { std::move(inventory) }
{
    batch_.island = inventory_.island;
}

bool SystemCKernelSafePointObserver::register_adapter(
    SystemCKernelChannelObservationAdapter adapter,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits_, diagnostics) || !adapter.endpoint.valid()
        || (!adapter.read && !adapter.write)
        || find_channel(inventory_, adapter.endpoint) == nullptr) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::metadata,
            "SystemC observation adapter does not name an inventoried channel");
    }
    if (adapters_.size() >= limits_.max_adapters) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::resource,
            "SystemC observation adapter limit is exhausted");
    }
    if (std::ranges::any_of(adapters_, [&](const auto& current) {
            return current.endpoint == adapter.endpoint;
        })) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::metadata,
            "SystemC observation adapter endpoint is duplicated");
    }
    adapters_.push_back(std::move(adapter));
    return true;
}

bool SystemCKernelSafePointObserver::query_inventory(
    const SystemCKernelExecutionOrder& order, diagnostic::Engine& diagnostics)
{
    if (!valid_safe_point(order, inventory_.island, diagnostics)) {
        return false;
    }
    const auto unsupported = std::ranges::count_if(inventory_.channels,
        [](const auto& entry) { return !entry.descriptor.supported; });
    return append({ SystemCKernelObservationKind::inventory_query, order, { },
                      { }, { }, std::nullopt, { },
                      "channels=" + std::to_string(inventory_.channels.size())
                          + "; unsupported="
                          + std::to_string(unsupported) },
        diagnostics);
}

std::optional<std::string> SystemCKernelSafePointObserver::report(
    const SystemCKernelExecutionOrder& order, diagnostic::Engine& diagnostics)
{
    if (!valid_safe_point(order, inventory_.island, diagnostics)) {
        return std::nullopt;
    }
    auto detail = std::string { "channels=" }
        + std::to_string(inventory_.channels.size())
        + "; adapters=" + std::to_string(adapters_.size())
        + "; records=" + std::to_string(batch_.records.size());
    if (!append({ SystemCKernelObservationKind::report, order, { }, { }, { },
                    std::nullopt, { }, detail },
            diagnostics)) {
        return std::nullopt;
    }
    return detail;
}

std::optional<SystemCKernelValue> SystemCKernelSafePointObserver::read(
    const SystemCEndpointId endpoint, const SystemCKernelExecutionOrder& order,
    diagnostic::Engine& diagnostics)
{
    if (!valid_safe_point(order, inventory_.island, diagnostics)) {
        return std::nullopt;
    }
    const auto found = std::ranges::find(
        adapters_, endpoint, &SystemCKernelChannelObservationAdapter::endpoint);
    if (found == adapters_.end() || !found->read) {
        report_error(diagnostics, SystemCKernelObservationCode::unsupported,
            "SystemC channel is visible but has no debugger read adapter");
        return std::nullopt;
    }
    if (batch_.records.size() >= limits_.max_records) {
        report_error(diagnostics, SystemCKernelObservationCode::resource,
            "SystemC observation record limit is exhausted");
        return std::nullopt;
    }
    if (!batch_.records.empty() && order < batch_.records.back().order) {
        report_error(diagnostics, SystemCKernelObservationCode::metadata,
            "SystemC observations are out of deterministic order");
        return std::nullopt;
    }
    auto value = found->read(diagnostics);
    if (!value
        || !validate_systemc_kernel_value(
            *value, limits_.value_limits, diagnostics)
        || !append({ SystemCKernelObservationKind::debugger_read, order,
                       endpoint, { }, { }, *value, { }, "safe-point read" },
            diagnostics)) {
        return std::nullopt;
    }
    return value;
}

bool SystemCKernelSafePointObserver::write(const SystemCEndpointId endpoint,
    const SystemCKernelValue& value, const SystemCKernelExecutionOrder& order,
    diagnostic::Engine& diagnostics)
{
    if (!valid_safe_point(order, inventory_.island, diagnostics)
        || !validate_systemc_kernel_value(
            value, limits_.value_limits, diagnostics)) {
        return false;
    }
    const auto found = std::ranges::find(
        adapters_, endpoint, &SystemCKernelChannelObservationAdapter::endpoint);
    if (found == adapters_.end() || !found->write) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::unsupported,
            "SystemC channel is visible but has no debugger write adapter");
    }
    if (batch_.records.size() >= limits_.max_records) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::resource,
            "SystemC observation record limit is exhausted");
    }
    if (!batch_.records.empty() && order < batch_.records.back().order) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::metadata,
            "SystemC observations are out of deterministic order");
    }
    if (!found->write(value, diagnostics)) {
        return false;
    }
    return append({ SystemCKernelObservationKind::debugger_write, order,
                      endpoint, { }, { }, value, { }, "safe-point write" },
        diagnostics);
}

bool SystemCKernelSafePointObserver::observe_tlm1(
    const SystemCKernelTlm1Transaction& value,
    const SystemCKernelObservationKind kind, diagnostic::Engine& diagnostics)
{
    if (kind < SystemCKernelObservationKind::tlm1_begin
        || kind > SystemCKernelObservationKind::tlm1_end) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::metadata,
            "SystemC TLM1 observation kind is invalid");
    }
    const auto state_matches
        = (kind == SystemCKernelObservationKind::tlm1_begin
              && value.state == SystemCKernelTlm1State::begun)
        || (kind == SystemCKernelObservationKind::tlm1_update
            && (value.state == SystemCKernelTlm1State::blocked
                || value.state == SystemCKernelTlm1State::unavailable))
        || (kind == SystemCKernelObservationKind::tlm1_end
            && (value.state == SystemCKernelTlm1State::completed
                || value.state == SystemCKernelTlm1State::canceled));
    if (value.explicit_bridge || !state_matches) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::metadata,
            "Native SystemC TLM1 activity and observation state disagree");
    }
    auto portable = value;
    portable.explicit_bridge = true;
    auto bytes = serialize_systemc_kernel_tlm1_transaction(
        portable, limits_.tlm1_limits, diagnostics);
    if (!bytes) {
        return false;
    }
    return append({ kind,
                      { value.time_fs, value.delta,
                          SystemCAccelleraRegion::quiescent, inventory_.island,
                          value.sequence },
                      value.endpoint, value.peer, value.transaction,
                      std::nullopt, std::move(*bytes),
                      "native in-island TLM1 transaction" },
        diagnostics);
}

bool SystemCKernelSafePointObserver::observe_tlm2(
    const SystemCKernelTlm2Transaction& value,
    const SystemCKernelObservationKind kind, diagnostic::Engine& diagnostics)
{
    if (kind < SystemCKernelObservationKind::tlm2_begin
        || kind > SystemCKernelObservationKind::tlm2_end) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::metadata,
            "SystemC TLM2 observation kind is invalid");
    }
    const auto state_matches
        = (kind == SystemCKernelObservationKind::tlm2_begin
              && value.state == SystemCKernelTlm2State::begun)
        || (kind == SystemCKernelObservationKind::tlm2_phase
            && value.operation
                >= SystemCKernelTlm2Operation::nonblocking_forward
            && value.operation
                <= SystemCKernelTlm2Operation::nonblocking_backward)
        || (kind == SystemCKernelObservationKind::tlm2_dmi
            && (value.dmi
                || value.operation
                    == SystemCKernelTlm2Operation::invalidate_direct_memory))
        || (kind == SystemCKernelObservationKind::tlm2_debug
            && value.operation
                == SystemCKernelTlm2Operation::debug_transport)
        || (kind == SystemCKernelObservationKind::tlm2_end
            && value.state >= SystemCKernelTlm2State::completed);
    if (value.explicit_bridge || !state_matches) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::metadata,
            "Native SystemC TLM2 activity and observation state disagree");
    }
    auto portable = value;
    portable.explicit_bridge = true;
    auto bytes = serialize_systemc_kernel_tlm2_transaction(
        portable, limits_.tlm2_limits, diagnostics);
    if (!bytes) {
        return false;
    }
    return append({ kind,
                      { value.time_fs, value.delta,
                          SystemCAccelleraRegion::quiescent, inventory_.island,
                          value.sequence },
                      value.endpoint, value.peer, value.transaction,
                      std::nullopt, std::move(*bytes),
                      "native in-island TLM2 transaction" },
        diagnostics);
}

const SystemCKernelChannelInventorySnapshot&
SystemCKernelSafePointObserver::inventory() const noexcept
{
    return inventory_;
}

const SystemCKernelObservationBatch& SystemCKernelSafePointObserver::batch()
    const noexcept
{
    return batch_;
}

bool SystemCKernelSafePointObserver::append(SystemCKernelObservationRecord record,
    diagnostic::Engine& diagnostics)
{
    if (batch_.records.size() >= limits_.max_records) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::resource,
            "SystemC observation record limit is exhausted");
    }
    if (!valid_record(record, batch_.island, limits_, diagnostics)) {
        return false;
    }
    if (!batch_.records.empty()
        && record.order < batch_.records.back().order) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::metadata,
            "SystemC observations are out of deterministic order");
    }
    batch_.records.push_back(std::move(record));
    return true;
}

bool validate_systemc_kernel_observation_batch(
    const SystemCKernelObservationBatch& batch,
    const SystemCKernelObservationLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics) || !batch.island.valid()
        || batch.records.size() > limits.max_records) {
        return report_error(diagnostics,
            SystemCKernelObservationCode::metadata,
            "SystemC observation batch identity or cardinality is invalid");
    }
    std::optional<SystemCKernelExecutionOrder> previous;
    for (const auto& record : batch.records) {
        if (!valid_record(record, batch.island, limits, diagnostics)
            || (previous && record.order < *previous)) {
            return report_error(diagnostics,
                SystemCKernelObservationCode::metadata,
                "SystemC observations are malformed or out of order");
        }
        previous = record.order;
    }
    return true;
}

std::optional<std::vector<std::byte>>
serialize_systemc_kernel_observation_batch(
    const SystemCKernelObservationBatch& batch,
    const SystemCKernelObservationLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_systemc_kernel_observation_batch(
            batch, limits, diagnostics)) {
        return std::nullopt;
    }
    std::vector<std::vector<std::byte>> values;
    values.reserve(batch.records.size());
    std::size_t size = kHeaderBytes;
    for (const auto& record : batch.records) {
        std::vector<std::byte> value_bytes;
        if (record.value) {
            auto encoded = serialize_systemc_kernel_value(
                *record.value, limits.value_limits, diagnostics);
            if (!encoded) {
                return std::nullopt;
            }
            value_bytes = std::move(*encoded);
        }
        if (value_bytes.size() > std::numeric_limits<std::uint32_t>::max()
            || record.transaction_bytes.size()
                > std::numeric_limits<std::uint32_t>::max()
            || record.detail.size() > std::numeric_limits<std::uint32_t>::max()
            || !checked_add(size, kRecordBytes)
            || !checked_add(size, value_bytes.size())
            || !checked_add(size, record.transaction_bytes.size())
            || !checked_add(size, record.detail.size())) {
            report_error(diagnostics, SystemCKernelObservationCode::resource,
                "SystemC observation encoding size overflows");
            return std::nullopt;
        }
        values.push_back(std::move(value_bytes));
    }
    if (size > limits.max_encoded_bytes
        || batch.records.size() > std::numeric_limits<std::uint32_t>::max()) {
        report_error(diagnostics, SystemCKernelObservationCode::resource,
            "SystemC observation encoding exceeds its governed limit");
        return std::nullopt;
    }
    Writer writer { size };
    writer.raw(kMagic);
    writer.u32(kSystemCKernelObservationVersion);
    writer.id(batch.island);
    writer.u32(static_cast<std::uint32_t>(batch.records.size()));
    writer.u32(0U);
    for (std::size_t index = 0U; index < batch.records.size(); ++index) {
        const auto& record = batch.records[index];
        const auto& value_bytes = values[index];
        writer.u8(static_cast<std::uint8_t>(record.kind));
        writer.u8(static_cast<std::uint8_t>(record.order.region));
        writer.u8(static_cast<std::uint8_t>((record.value ? 1U : 0U)
            | (!record.transaction_bytes.empty() ? 2U : 0U)));
        writer.u8(0U);
        writer.u64(record.order.time_fs);
        writer.u64(record.order.delta);
        writer.id(record.order.island);
        writer.u64(record.order.sequence.value);
        writer.id(record.endpoint);
        writer.id(record.peer);
        writer.id(record.transaction);
        writer.u32(static_cast<std::uint32_t>(value_bytes.size()));
        writer.u32(
            static_cast<std::uint32_t>(record.transaction_bytes.size()));
        writer.u32(static_cast<std::uint32_t>(record.detail.size()));
        writer.u32(0U);
        writer.raw(value_bytes);
        writer.raw(record.transaction_bytes);
        writer.text(record.detail);
    }
    return std::move(writer).take();
}

std::optional<SystemCKernelObservationBatch>
deserialize_systemc_kernel_observation_batch(
    const std::span<const std::byte> bytes,
    const SystemCKernelObservationLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics) || bytes.size() < kHeaderBytes
        || bytes.size() > limits.max_encoded_bytes) {
        report_error(diagnostics, SystemCKernelObservationCode::resource,
            "SystemC observation bytes are truncated or over budget");
        return std::nullopt;
    }
    Reader reader { bytes };
    std::span<const std::byte> magic;
    std::uint32_t version { };
    std::uint32_t count { };
    std::uint32_t reserved { };
    SystemCKernelObservationBatch batch;
    if (!reader.raw(kMagic.size(), magic) || !std::ranges::equal(magic, kMagic)
        || !reader.u32(version) || !reader.id(batch.island)
        || !reader.u32(count) || !reader.u32(reserved)
        || version != kSystemCKernelObservationVersion || reserved != 0U
        || count > limits.max_records || count > reader.remaining() / kRecordBytes) {
        report_error(diagnostics, SystemCKernelObservationCode::metadata,
            "SystemC observation header is malformed or noncanonical");
        return std::nullopt;
    }
    batch.records.reserve(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        SystemCKernelObservationRecord record;
        std::uint8_t kind { };
        std::uint8_t region { };
        std::uint8_t flags { };
        std::uint8_t byte_reserved { };
        std::uint32_t value_size { };
        std::uint32_t transaction_size { };
        std::uint32_t detail_size { };
        std::span<const std::byte> value_bytes;
        std::span<const std::byte> transaction_bytes;
        if (!reader.u8(kind) || !reader.u8(region) || !reader.u8(flags)
            || !reader.u8(byte_reserved) || !reader.u64(record.order.time_fs)
            || !reader.u64(record.order.delta) || !reader.id(record.order.island)
            || !reader.u64(record.order.sequence.value)
            || !reader.id(record.endpoint) || !reader.id(record.peer)
            || !reader.id(record.transaction) || !reader.u32(value_size)
            || !reader.u32(transaction_size) || !reader.u32(detail_size)
            || !reader.u32(reserved) || byte_reserved != 0U || reserved != 0U
            || (flags & 0xfcU) != 0U
            || ((flags & 1U) == 0U) != (value_size == 0U)
            || ((flags & 2U) == 0U) != (transaction_size == 0U)
            || transaction_size > limits.max_transaction_bytes
            || detail_size > limits.max_detail_bytes
            || !reader.raw(value_size, value_bytes)
            || !reader.raw(transaction_size, transaction_bytes)
            || !reader.text(detail_size, record.detail)) {
            report_error(diagnostics, SystemCKernelObservationCode::metadata,
                "SystemC observation record is truncated or malformed");
            return std::nullopt;
        }
        record.kind = static_cast<SystemCKernelObservationKind>(kind);
        record.order.region = static_cast<SystemCAccelleraRegion>(region);
        if (!value_bytes.empty()) {
            record.value = deserialize_systemc_kernel_value(
                value_bytes, limits.value_limits, diagnostics);
            if (!record.value) {
                return std::nullopt;
            }
        }
        record.transaction_bytes.assign(
            transaction_bytes.begin(), transaction_bytes.end());
        batch.records.push_back(std::move(record));
    }
    if (reader.remaining() != 0U
        || !validate_systemc_kernel_observation_batch(
            batch, limits, diagnostics)) {
        if (!diagnostics.has_error()) {
            report_error(diagnostics, SystemCKernelObservationCode::metadata,
                "SystemC observation encoding has trailing bytes");
        }
        return std::nullopt;
    }
    return batch;
}

const char* systemc_kernel_observation_diagnostic_code(
    const SystemCKernelObservationCode code) noexcept
{
    switch (code) {
    case SystemCKernelObservationCode::none:
        return "";
    case SystemCKernelObservationCode::metadata:
        return "FSIM-SC-Y001";
    case SystemCKernelObservationCode::safe_point:
        return "FSIM-SC-Y002";
    case SystemCKernelObservationCode::unsupported:
        return "FSIM-SC-Y003";
    case SystemCKernelObservationCode::resource:
        return "FSIM-SC-Y004";
    }
    return "FSIM-SC-Y001";
}

} // namespace fsim::systemc
