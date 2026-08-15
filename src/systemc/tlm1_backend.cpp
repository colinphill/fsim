// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_tlm1.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>

namespace fsim::systemc {
namespace {

    constexpr std::array<std::byte, 4> kMagic { std::byte { 'F' },
        std::byte { 'S' }, std::byte { 'T' }, std::byte { '1' } };
    constexpr std::size_t kHeaderBytes = 96U;

    bool report_error(diagnostic::Engine& diagnostics,
        const SystemCKernelTlm1Code code, const std::string_view message)
    {
        diagnostics.error(systemc_kernel_tlm1_diagnostic_code(code),
            std::string { message });
        return false;
    }

    bool valid_limits(const SystemCKernelTlm1Limits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_endpoints == 0U
            || limits.max_peers_per_endpoint == 0U
            || limits.max_transactions == 0U
            || limits.max_type_name_bytes == 0U
            || limits.max_encoded_bytes < kHeaderBytes) {
            return report_error(diagnostics, SystemCKernelTlm1Code::resource,
                "SystemC TLM1 limits must be nonzero and contain the header");
        }
        return true;
    }

    bool known_kind(const SystemCKernelTlm1InterfaceKind kind) noexcept
    {
        const auto value = static_cast<std::uint8_t>(kind);
        return value >= static_cast<std::uint8_t>(
                            SystemCKernelTlm1InterfaceKind::fifo)
            && value <= static_cast<std::uint8_t>(
                SystemCKernelTlm1InterfaceKind::analysis);
    }

    bool known_operation(const SystemCKernelTlm1Operation operation) noexcept
    {
        switch (operation) {
        case SystemCKernelTlm1Operation::put:
        case SystemCKernelTlm1Operation::get:
        case SystemCKernelTlm1Operation::peek:
        case SystemCKernelTlm1Operation::transport:
        case SystemCKernelTlm1Operation::analysis:
            return true;
        }
        return false;
    }

    bool known_state(const SystemCKernelTlm1State state) noexcept
    {
        switch (state) {
        case SystemCKernelTlm1State::begun:
        case SystemCKernelTlm1State::blocked:
        case SystemCKernelTlm1State::completed:
        case SystemCKernelTlm1State::unavailable:
        case SystemCKernelTlm1State::canceled:
            return true;
        }
        return false;
    }

    bool supports_operation(const SystemCKernelTlm1InterfaceKind kind,
        const SystemCKernelTlm1Operation operation) noexcept
    {
        if (kind == SystemCKernelTlm1InterfaceKind::fifo) {
            return operation == SystemCKernelTlm1Operation::put
                || operation == SystemCKernelTlm1Operation::get
                || operation == SystemCKernelTlm1Operation::peek;
        }
        if (operation == SystemCKernelTlm1Operation::put) {
            return kind == SystemCKernelTlm1InterfaceKind::blocking_put
                || kind == SystemCKernelTlm1InterfaceKind::nonblocking_put;
        }
        if (operation == SystemCKernelTlm1Operation::get) {
            return kind == SystemCKernelTlm1InterfaceKind::blocking_get
                || kind == SystemCKernelTlm1InterfaceKind::nonblocking_get;
        }
        if (operation == SystemCKernelTlm1Operation::peek) {
            return kind == SystemCKernelTlm1InterfaceKind::blocking_peek
                || kind == SystemCKernelTlm1InterfaceKind::nonblocking_peek;
        }
        if (operation == SystemCKernelTlm1Operation::transport) {
            return kind == SystemCKernelTlm1InterfaceKind::transport;
        }
        return operation == SystemCKernelTlm1Operation::analysis
            && kind == SystemCKernelTlm1InterfaceKind::analysis;
    }

    bool request_required(const SystemCKernelTlm1Operation operation) noexcept
    {
        return operation == SystemCKernelTlm1Operation::put
            || operation == SystemCKernelTlm1Operation::transport
            || operation == SystemCKernelTlm1Operation::analysis;
    }

    bool response_required(const SystemCKernelTlm1Operation operation) noexcept
    {
        return operation == SystemCKernelTlm1Operation::get
            || operation == SystemCKernelTlm1Operation::peek
            || operation == SystemCKernelTlm1Operation::transport;
    }

    bool valid_payload_shape(const SystemCKernelTlm1Transaction& transaction,
        diagnostic::Engine& diagnostics)
    {
        if (request_required(transaction.operation)
            != transaction.request.has_value()) {
            return report_error(diagnostics, SystemCKernelTlm1Code::payload,
                "SystemC TLM1 request payload does not match its operation");
        }
        const auto terminal_response
            = transaction.state == SystemCKernelTlm1State::completed
            && response_required(transaction.operation);
        if (terminal_response != transaction.response.has_value()) {
            return report_error(diagnostics, SystemCKernelTlm1Code::payload,
                "SystemC TLM1 response payload does not match its state");
        }
        return true;
    }

    bool valid_transition(const SystemCKernelTlm1State before,
        const SystemCKernelTlm1State after) noexcept
    {
        if (before == SystemCKernelTlm1State::begun) {
            return after == SystemCKernelTlm1State::blocked
                || after == SystemCKernelTlm1State::completed
                || after == SystemCKernelTlm1State::unavailable
                || after == SystemCKernelTlm1State::canceled;
        }
        return before == SystemCKernelTlm1State::blocked
            && (after == SystemCKernelTlm1State::completed
                || after == SystemCKernelTlm1State::canceled);
    }

    class Writer {
    public:
        explicit Writer(const std::size_t reserve) { bytes_.reserve(reserve); }

        void u8(const std::uint8_t value)
        {
            bytes_.push_back(std::byte { value });
        }
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
        bool raw(const std::size_t size, std::span<const std::byte>& result)
        {
            if (size > bytes_.size() - offset_) {
                return false;
            }
            result = bytes_.subspan(offset_, size);
            offset_ += size;
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

    const SystemCKernelTlm1Endpoint* find_endpoint(
        const std::vector<SystemCKernelTlm1Endpoint>& endpoints,
        const SystemCEndpointId id)
    {
        const auto found = std::ranges::find(endpoints, id,
            &SystemCKernelTlm1Endpoint::endpoint);
        return found == endpoints.end() ? nullptr : &*found;
    }

    SystemCKernelTlm1Transaction* find_transaction(
        std::vector<SystemCKernelTlm1Transaction>& transactions,
        const SystemCTransactionId id)
    {
        const auto found = std::ranges::find(transactions, id,
            &SystemCKernelTlm1Transaction::transaction);
        return found == transactions.end() ? nullptr : &*found;
    }

} // namespace

SystemCKernelTlm1Registry::SystemCKernelTlm1Registry(
    const SystemCIslandId island, SystemCKernelTlm1Limits limits)
    : island_ { island }
    , limits_ { std::move(limits) }
{
}

bool SystemCKernelTlm1Registry::register_endpoint(
    SystemCKernelTlm1Endpoint endpoint, diagnostic::Engine& diagnostics)
{
    if (!island_.valid() || !valid_limits(limits_, diagnostics)
        || !validate_systemc_kernel_tlm1_endpoint(
            endpoint, limits_, diagnostics)) {
        return false;
    }
    if (endpoints_.size() >= limits_.max_endpoints) {
        return report_error(diagnostics, SystemCKernelTlm1Code::resource,
            "SystemC TLM1 endpoint registry exceeds its governed limit");
    }
    if (find_endpoint(endpoints_, endpoint.endpoint) != nullptr) {
        return report_error(diagnostics, SystemCKernelTlm1Code::metadata,
            "SystemC TLM1 endpoint identity is already registered");
    }
    endpoints_.push_back(std::move(endpoint));
    return true;
}

std::optional<SystemCTransactionId> SystemCKernelTlm1Registry::begin(
    const SystemCEndpointId endpoint, const SystemCEndpointId peer,
    const SystemCSequenceId sequence,
    const SystemCKernelTlm1Operation operation,
    std::optional<SystemCKernelValue> request, const std::uint64_t time_fs,
    const std::uint64_t delta, diagnostic::Engine& diagnostics)
{
    const auto* source = find_endpoint(endpoints_, endpoint);
    const auto* target = find_endpoint(endpoints_, peer);
    if (source == nullptr || target == nullptr
        || std::ranges::find(source->peers, peer) == source->peers.end()
        || std::ranges::find(target->peers, endpoint) == target->peers.end()) {
        report_error(diagnostics, SystemCKernelTlm1Code::metadata,
            "SystemC TLM1 transaction endpoints are not directly connected");
        return std::nullopt;
    }
    if (!supports_operation(source->kind, operation)
        || !supports_operation(target->kind, operation)) {
        report_error(diagnostics, SystemCKernelTlm1Code::metadata,
            "SystemC TLM1 endpoint does not support the requested operation");
        return std::nullopt;
    }
    if (source->explicit_bridge != target->explicit_bridge) {
        report_error(diagnostics, SystemCKernelTlm1Code::metadata,
            "SystemC TLM1 connected endpoints disagree on bridge ownership");
        return std::nullopt;
    }
    if (transactions_.size() >= limits_.max_transactions) {
        report_error(diagnostics, SystemCKernelTlm1Code::resource,
            "SystemC TLM1 transaction registry exceeds its governed limit");
        return std::nullopt;
    }
    const auto transaction
        = make_systemc_transaction_id(endpoint, sequence, diagnostics);
    if (!transaction
        || find_transaction(transactions_, *transaction) != nullptr) {
        report_error(diagnostics, SystemCKernelTlm1Code::lifecycle,
            "SystemC TLM1 transaction identity is invalid or repeated");
        return std::nullopt;
    }
    SystemCKernelTlm1Transaction record { *transaction, endpoint, peer,
        sequence, operation, SystemCKernelTlm1State::begun, time_fs, delta,
        source->explicit_bridge, std::move(request),
        { } };
    if (!validate_systemc_kernel_tlm1_transaction(
            record, limits_, diagnostics)) {
        return std::nullopt;
    }
    transactions_.push_back(std::move(record));
    return transaction;
}

bool SystemCKernelTlm1Registry::transition(
    const SystemCTransactionId transaction,
    const SystemCKernelTlm1State state,
    std::optional<SystemCKernelValue> response,
    diagnostic::Engine& diagnostics)
{
    auto* record = find_transaction(transactions_, transaction);
    if (record == nullptr || !known_state(state)
        || !valid_transition(record == nullptr
                ? SystemCKernelTlm1State::completed
                : record->state,
            state)) {
        return report_error(diagnostics, SystemCKernelTlm1Code::lifecycle,
            "SystemC TLM1 transaction transition is stale or illegal");
    }
    auto proposed = *record;
    proposed.state = state;
    proposed.response = std::move(response);
    if (!validate_systemc_kernel_tlm1_transaction(
            proposed, limits_, diagnostics)) {
        return false;
    }
    *record = std::move(proposed);
    return true;
}

const std::vector<SystemCKernelTlm1Endpoint>&
SystemCKernelTlm1Registry::endpoints() const noexcept
{
    return endpoints_;
}

const std::vector<SystemCKernelTlm1Transaction>&
SystemCKernelTlm1Registry::transactions() const noexcept
{
    return transactions_;
}

bool validate_systemc_kernel_tlm1_endpoint(
    const SystemCKernelTlm1Endpoint& endpoint,
    const SystemCKernelTlm1Limits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return false;
    }
    if (!endpoint.endpoint.valid() || !known_kind(endpoint.kind)
        || endpoint.type_name.empty()
        || endpoint.type_name.size() > limits.max_type_name_bytes) {
        return report_error(diagnostics, SystemCKernelTlm1Code::metadata,
            "SystemC TLM1 endpoint metadata is incomplete or unsupported");
    }
    if (endpoint.peers.empty()
        || endpoint.peers.size() > limits.max_peers_per_endpoint
        || std::ranges::any_of(endpoint.peers,
            [&](const auto peer) {
                return !peer.valid() || peer == endpoint.endpoint;
            })) {
        return report_error(diagnostics, SystemCKernelTlm1Code::metadata,
            "SystemC TLM1 endpoint peers are invalid or exceed their limit");
    }
    auto peers = endpoint.peers;
    std::ranges::sort(peers);
    if (std::ranges::adjacent_find(peers) != peers.end()) {
        return report_error(diagnostics, SystemCKernelTlm1Code::metadata,
            "SystemC TLM1 endpoint peers must be unique");
    }
    return true;
}

bool validate_systemc_kernel_tlm1_transaction(
    const SystemCKernelTlm1Transaction& transaction,
    const SystemCKernelTlm1Limits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)
        || !transaction.transaction.valid() || !transaction.endpoint.valid()
        || !transaction.peer.valid() || transaction.peer == transaction.endpoint
        || !transaction.sequence.valid() || !known_operation(transaction.operation)
        || !known_state(transaction.state)) {
        return report_error(diagnostics, SystemCKernelTlm1Code::metadata,
            "SystemC TLM1 transaction metadata is incomplete or unsupported");
    }
    const auto expected = make_systemc_transaction_id(
        transaction.endpoint, transaction.sequence, diagnostics);
    if (!expected || *expected != transaction.transaction) {
        return report_error(diagnostics, SystemCKernelTlm1Code::metadata,
            "SystemC TLM1 transaction identity is noncanonical");
    }
    if (!valid_payload_shape(transaction, diagnostics)) {
        return false;
    }
    if (transaction.request
        && !validate_systemc_kernel_value(
            *transaction.request, limits.value_limits, diagnostics)) {
        return false;
    }
    if (transaction.response
        && !validate_systemc_kernel_value(
            *transaction.response, limits.value_limits, diagnostics)) {
        return false;
    }
    return true;
}

std::optional<std::vector<std::byte>>
serialize_systemc_kernel_tlm1_transaction(
    const SystemCKernelTlm1Transaction& transaction,
    const SystemCKernelTlm1Limits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_systemc_kernel_tlm1_transaction(
            transaction, limits, diagnostics)) {
        return std::nullopt;
    }
    if (!transaction.explicit_bridge) {
        report_error(diagnostics, SystemCKernelTlm1Code::lifecycle,
            "Native in-island SystemC TLM1 traffic must not be serialized");
        return std::nullopt;
    }
    const auto request = transaction.request
        ? serialize_systemc_kernel_value(
              *transaction.request, limits.value_limits, diagnostics)
        : std::optional<std::vector<std::byte>> { std::vector<std::byte> { } };
    const auto response = transaction.response
        ? serialize_systemc_kernel_value(
              *transaction.response, limits.value_limits, diagnostics)
        : std::optional<std::vector<std::byte>> { std::vector<std::byte> { } };
    if (!request || !response
        || request->size() > std::numeric_limits<std::uint32_t>::max()
        || response->size() > std::numeric_limits<std::uint32_t>::max()
        || request->size() > limits.max_encoded_bytes - kHeaderBytes
        || response->size()
            > limits.max_encoded_bytes - kHeaderBytes - request->size()) {
        report_error(diagnostics, SystemCKernelTlm1Code::resource,
            "SystemC TLM1 transaction encoding exceeds its governed limit");
        return std::nullopt;
    }
    Writer writer { kHeaderBytes + request->size() + response->size() };
    writer.raw(kMagic);
    writer.u32(kSystemCKernelTlm1Version);
    writer.u8(static_cast<std::uint8_t>(transaction.operation));
    writer.u8(static_cast<std::uint8_t>(transaction.state));
    writer.u8(1U);
    writer.u8(transaction.request ? 1U : 0U);
    writer.u8(transaction.response ? 1U : 0U);
    writer.u8(0U);
    writer.u8(0U);
    writer.u8(0U);
    writer.id(transaction.transaction);
    writer.id(transaction.endpoint);
    writer.id(transaction.peer);
    writer.u64(transaction.sequence.value);
    writer.u64(transaction.time_fs);
    writer.u64(transaction.delta);
    writer.u32(static_cast<std::uint32_t>(request->size()));
    writer.u32(static_cast<std::uint32_t>(response->size()));
    writer.raw(*request);
    writer.raw(*response);
    return std::move(writer).take();
}

std::optional<SystemCKernelTlm1Transaction>
deserialize_systemc_kernel_tlm1_transaction(
    const std::span<const std::byte> bytes,
    const SystemCKernelTlm1Limits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return std::nullopt;
    }
    if (bytes.size() < kHeaderBytes || bytes.size() > limits.max_encoded_bytes) {
        report_error(diagnostics, SystemCKernelTlm1Code::resource,
            "SystemC TLM1 transaction bytes are truncated or over budget");
        return std::nullopt;
    }
    Reader reader { bytes };
    std::span<const std::byte> magic;
    std::uint32_t version { };
    std::uint8_t operation { };
    std::uint8_t state { };
    std::uint8_t bridge { };
    std::uint8_t has_request { };
    std::uint8_t has_response { };
    std::uint8_t reserved0 { };
    std::uint8_t reserved1 { };
    std::uint8_t reserved2 { };
    SystemCKernelTlm1Transaction transaction;
    std::uint32_t request_size { };
    std::uint32_t response_size { };
    if (!reader.raw(kMagic.size(), magic) || magic.size() != kMagic.size()
        || !std::ranges::equal(magic, kMagic) || !reader.u32(version)
        || !reader.u8(operation) || !reader.u8(state) || !reader.u8(bridge)
        || !reader.u8(has_request) || !reader.u8(has_response)
        || !reader.u8(reserved0) || !reader.u8(reserved1)
        || !reader.u8(reserved2) || !reader.id(transaction.transaction)
        || !reader.id(transaction.endpoint) || !reader.id(transaction.peer)
        || !reader.u64(transaction.sequence.value)
        || !reader.u64(transaction.time_fs) || !reader.u64(transaction.delta)
        || !reader.u32(request_size) || !reader.u32(response_size)
        || version != kSystemCKernelTlm1Version || bridge != 1U
        || has_request > 1U || has_response > 1U || reserved0 != 0U
        || reserved1 != 0U || reserved2 != 0U
        || request_size > reader.remaining()
        || response_size > reader.remaining() - request_size
        || static_cast<std::size_t>(request_size)
                + static_cast<std::size_t>(response_size)
            != reader.remaining()) {
        report_error(diagnostics, SystemCKernelTlm1Code::payload,
            "SystemC TLM1 transaction encoding is malformed or noncanonical");
        return std::nullopt;
    }
    transaction.operation = static_cast<SystemCKernelTlm1Operation>(operation);
    transaction.state = static_cast<SystemCKernelTlm1State>(state);
    transaction.explicit_bridge = true;
    std::span<const std::byte> request_bytes;
    std::span<const std::byte> response_bytes;
    if (!reader.raw(request_size, request_bytes)
        || !reader.raw(response_size, response_bytes)
        || (has_request == 0U) != request_bytes.empty()
        || (has_response == 0U) != response_bytes.empty()) {
        report_error(diagnostics, SystemCKernelTlm1Code::payload,
            "SystemC TLM1 transaction payload presence is noncanonical");
        return std::nullopt;
    }
    if (has_request != 0U) {
        transaction.request = deserialize_systemc_kernel_value(
            request_bytes, limits.value_limits, diagnostics);
        if (!transaction.request) {
            return std::nullopt;
        }
    }
    if (has_response != 0U) {
        transaction.response = deserialize_systemc_kernel_value(
            response_bytes, limits.value_limits, diagnostics);
        if (!transaction.response) {
            return std::nullopt;
        }
    }
    if (!validate_systemc_kernel_tlm1_transaction(
            transaction, limits, diagnostics)) {
        return std::nullopt;
    }
    return transaction;
}

const char* systemc_kernel_tlm1_diagnostic_code(
    const SystemCKernelTlm1Code code) noexcept
{
    switch (code) {
    case SystemCKernelTlm1Code::none:
        return "";
    case SystemCKernelTlm1Code::metadata:
        return "FSIM-SC-T001";
    case SystemCKernelTlm1Code::lifecycle:
        return "FSIM-SC-T002";
    case SystemCKernelTlm1Code::payload:
        return "FSIM-SC-T003";
    case SystemCKernelTlm1Code::resource:
        return "FSIM-SC-T004";
    }
    return "FSIM-SC-T001";
}

} // namespace fsim::systemc
