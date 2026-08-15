// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_tlm2.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>

namespace fsim::systemc {
namespace {

    constexpr std::array<std::byte, 4> kMagic { std::byte { 'F' },
        std::byte { 'S' }, std::byte { 'T' }, std::byte { '2' } };
    constexpr std::size_t kHeaderBytes = 160U;

    bool report_error(diagnostic::Engine& diagnostics,
        const SystemCKernelTlm2Code code, const std::string_view message)
    {
        diagnostics.error(systemc_kernel_tlm2_diagnostic_code(code),
            std::string { message });
        return false;
    }

    bool valid_limits(const SystemCKernelTlm2Limits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_endpoints == 0U || limits.max_peers_per_endpoint == 0U
            || limits.max_transactions == 0U || limits.max_data_bytes == 0U
            || limits.max_byte_enable_bytes == 0U
            || limits.max_extensions == 0U
            || limits.max_extension_type_bytes == 0U
            || limits.max_extension_bytes == 0U
            || limits.max_encoded_bytes < kHeaderBytes) {
            return report_error(diagnostics, SystemCKernelTlm2Code::resource,
                "SystemC TLM2 limits must be nonzero and contain the header");
        }
        return true;
    }

    template <typename Enum>
    bool in_closed_range(const Enum value, const Enum first, const Enum last) noexcept
    {
        const auto raw = static_cast<std::uint8_t>(value);
        return raw >= static_cast<std::uint8_t>(first)
            && raw <= static_cast<std::uint8_t>(last);
    }

    bool known_socket(const SystemCKernelTlm2SocketKind value) noexcept
    {
        return in_closed_range(value, SystemCKernelTlm2SocketKind::initiator,
            SystemCKernelTlm2SocketKind::multi_passthrough_target);
    }

    bool known_command(const SystemCKernelTlm2Command value) noexcept
    {
        return in_closed_range(value, SystemCKernelTlm2Command::ignore,
            SystemCKernelTlm2Command::write);
    }

    bool known_response(const SystemCKernelTlm2Response value) noexcept
    {
        return in_closed_range(value, SystemCKernelTlm2Response::incomplete,
            SystemCKernelTlm2Response::byte_enable_error);
    }

    bool known_operation(const SystemCKernelTlm2Operation value) noexcept
    {
        return in_closed_range(value,
            SystemCKernelTlm2Operation::blocking_transport,
            SystemCKernelTlm2Operation::invalidate_direct_memory);
    }

    bool known_phase(const SystemCKernelTlm2Phase value) noexcept
    {
        return in_closed_range(value, SystemCKernelTlm2Phase::none,
            SystemCKernelTlm2Phase::end_response);
    }

    bool known_sync(const SystemCKernelTlm2Sync value) noexcept
    {
        return in_closed_range(value, SystemCKernelTlm2Sync::accepted,
            SystemCKernelTlm2Sync::completed);
    }

    bool known_access(const SystemCKernelTlm2DmiAccess value) noexcept
    {
        return in_closed_range(value, SystemCKernelTlm2DmiAccess::none,
            SystemCKernelTlm2DmiAccess::read_write);
    }

    bool known_state(const SystemCKernelTlm2State value) noexcept
    {
        return in_closed_range(value, SystemCKernelTlm2State::begun,
            SystemCKernelTlm2State::canceled);
    }

    bool initiator_socket(const SystemCKernelTlm2SocketKind kind) noexcept
    {
        return kind == SystemCKernelTlm2SocketKind::initiator
            || kind == SystemCKernelTlm2SocketKind::multi_passthrough_initiator;
    }

    bool target_socket(const SystemCKernelTlm2SocketKind kind) noexcept
    {
        return kind == SystemCKernelTlm2SocketKind::target
            || kind == SystemCKernelTlm2SocketKind::multi_passthrough_target;
    }

    bool compatible_direction(const SystemCKernelTlm2SocketKind source,
        const SystemCKernelTlm2SocketKind target,
        const SystemCKernelTlm2Operation operation) noexcept
    {
        const bool backward
            = operation == SystemCKernelTlm2Operation::nonblocking_backward
            || operation
                == SystemCKernelTlm2Operation::invalidate_direct_memory;
        return backward ? target_socket(source) && initiator_socket(target)
                        : initiator_socket(source) && target_socket(target);
    }

    bool valid_transition(const SystemCKernelTlm2State before,
        const SystemCKernelTlm2State after) noexcept
    {
        if (before == SystemCKernelTlm2State::begun) {
            return after == SystemCKernelTlm2State::updated
                || after == SystemCKernelTlm2State::completed
                || after == SystemCKernelTlm2State::rejected
                || after == SystemCKernelTlm2State::canceled;
        }
        return before == SystemCKernelTlm2State::updated
            && (after == SystemCKernelTlm2State::updated
                || after == SystemCKernelTlm2State::completed
                || after == SystemCKernelTlm2State::rejected
                || after == SystemCKernelTlm2State::canceled);
    }

    bool valid_operation_shape(const SystemCKernelTlm2Transaction& transaction,
        diagnostic::Engine& diagnostics)
    {
        const bool nonblocking
            = transaction.operation
                == SystemCKernelTlm2Operation::nonblocking_forward
            || transaction.operation
                == SystemCKernelTlm2Operation::nonblocking_backward;
        if (nonblocking == (transaction.phase == SystemCKernelTlm2Phase::none)) {
            return report_error(diagnostics, SystemCKernelTlm2Code::payload,
                "SystemC TLM2 phase does not match its operation");
        }
        const bool dmi_operation
            = transaction.operation == SystemCKernelTlm2Operation::direct_memory
            || transaction.operation
                == SystemCKernelTlm2Operation::invalidate_direct_memory;
        if (transaction.dmi.has_value() && !dmi_operation) {
            return report_error(diagnostics, SystemCKernelTlm2Code::payload,
                "SystemC TLM2 DMI metadata does not match its operation");
        }
        if (transaction.operation
                == SystemCKernelTlm2Operation::invalidate_direct_memory
            && !transaction.dmi) {
            return report_error(diagnostics, SystemCKernelTlm2Code::payload,
                "SystemC TLM2 invalidation requires an address range");
        }
        if (transaction.transferred > transaction.payload.data.size()) {
            return report_error(diagnostics, SystemCKernelTlm2Code::payload,
                "SystemC TLM2 debug transfer count exceeds its payload");
        }
        return true;
    }

    bool valid_dmi(const SystemCKernelTlm2Dmi& dmi,
        diagnostic::Engine& diagnostics)
    {
        if (dmi.start_address > dmi.end_address || !known_access(dmi.access)) {
            return report_error(diagnostics, SystemCKernelTlm2Code::payload,
                "SystemC TLM2 DMI range or access is invalid");
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
        bool raw(const std::size_t size, std::span<const std::byte>& result)
        {
            if (size > remaining()) {
                return false;
            }
            result = bytes_.subspan(offset_, size);
            offset_ += size;
            return true;
        }
        bool bytes(const std::size_t size, std::vector<std::byte>& result)
        {
            std::span<const std::byte> source;
            if (!raw(size, source)) {
                return false;
            }
            result.assign(source.begin(), source.end());
            return true;
        }
        bool text(const std::size_t size, std::string& result)
        {
            std::span<const std::byte> source;
            if (!raw(size, source)) {
                return false;
            }
            result.assign(reinterpret_cast<const char*>(source.data()), source.size());
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

    const SystemCKernelTlm2Endpoint* find_endpoint(
        const std::vector<SystemCKernelTlm2Endpoint>& endpoints,
        const SystemCEndpointId id)
    {
        const auto found = std::ranges::find(endpoints, id,
            &SystemCKernelTlm2Endpoint::endpoint);
        return found == endpoints.end() ? nullptr : &*found;
    }

    SystemCKernelTlm2Transaction* find_transaction(
        std::vector<SystemCKernelTlm2Transaction>& transactions,
        const SystemCTransactionId id)
    {
        const auto found = std::ranges::find(transactions, id,
            &SystemCKernelTlm2Transaction::transaction);
        return found == transactions.end() ? nullptr : &*found;
    }

    std::optional<std::size_t> extension_bytes(
        const SystemCKernelTlm2Payload& payload)
    {
        std::size_t result { };
        for (const auto& extension : payload.extensions) {
            if (extension.type_name.size()
                    > std::numeric_limits<std::uint32_t>::max()
                || extension.bytes.size()
                    > std::numeric_limits<std::uint32_t>::max()
                || extension.type_name.size()
                    > std::numeric_limits<std::size_t>::max() - 8U
                || extension.bytes.size()
                    > std::numeric_limits<std::size_t>::max() - 8U
                        - extension.type_name.size()
                || result > std::numeric_limits<std::size_t>::max() - 8U
                        - extension.type_name.size() - extension.bytes.size()) {
                return std::nullopt;
            }
            result += 8U + extension.type_name.size() + extension.bytes.size();
        }
        return result;
    }

    bool read_extensions(Reader& reader, const std::uint32_t count,
        const SystemCKernelTlm2Limits& limits,
        SystemCKernelTlm2Payload& payload, diagnostic::Engine& diagnostics)
    {
        payload.extensions.reserve(count);
        for (std::uint32_t index = 0U; index < count; ++index) {
            std::uint32_t type_size { };
            std::uint32_t extension_size { };
            SystemCKernelTlm2Extension extension;
            if (!reader.u32(type_size) || !reader.u32(extension_size)
                || type_size > limits.max_extension_type_bytes
                || extension_size > limits.max_extension_bytes
                || !reader.text(type_size, extension.type_name)
                || !reader.bytes(extension_size, extension.bytes)) {
                return report_error(diagnostics,
                    SystemCKernelTlm2Code::payload,
                    "SystemC TLM2 extension encoding is truncated or over budget");
            }
            payload.extensions.push_back(std::move(extension));
        }
        return true;
    }

} // namespace

SystemCKernelTlm2Registry::SystemCKernelTlm2Registry(
    const SystemCIslandId island, SystemCKernelTlm2Limits limits)
    : island_ { island }
    , limits_ { std::move(limits) }
{
}

bool SystemCKernelTlm2Registry::register_endpoint(
    SystemCKernelTlm2Endpoint endpoint, diagnostic::Engine& diagnostics)
{
    if (!island_.valid()
        || !validate_systemc_kernel_tlm2_endpoint(
            endpoint, limits_, diagnostics)) {
        return false;
    }
    if (endpoints_.size() >= limits_.max_endpoints) {
        return report_error(diagnostics, SystemCKernelTlm2Code::resource,
            "SystemC TLM2 endpoint registry exceeds its governed limit");
    }
    if (find_endpoint(endpoints_, endpoint.endpoint) != nullptr) {
        return report_error(diagnostics, SystemCKernelTlm2Code::metadata,
            "SystemC TLM2 endpoint identity is already registered");
    }
    endpoints_.push_back(std::move(endpoint));
    return true;
}

std::optional<SystemCTransactionId> SystemCKernelTlm2Registry::begin(
    const SystemCEndpointId endpoint, const SystemCEndpointId peer,
    const SystemCSequenceId sequence,
    const SystemCKernelTlm2Operation operation,
    const SystemCKernelTlm2Phase phase, SystemCKernelTlm2Payload payload,
    const std::uint64_t time_fs, const std::uint64_t delta,
    diagnostic::Engine& diagnostics)
{
    const auto* source = find_endpoint(endpoints_, endpoint);
    const auto* target = find_endpoint(endpoints_, peer);
    if (source == nullptr || target == nullptr
        || std::ranges::find(source->peers, peer) == source->peers.end()
        || std::ranges::find(target->peers, endpoint) == target->peers.end()) {
        report_error(diagnostics, SystemCKernelTlm2Code::metadata,
            "SystemC TLM2 transaction endpoints are not directly connected");
        return std::nullopt;
    }
    if (!compatible_direction(source->kind, target->kind, operation)
        || source->bus_width_bits != target->bus_width_bits
        || source->explicit_bridge != target->explicit_bridge) {
        report_error(diagnostics, SystemCKernelTlm2Code::metadata,
            "SystemC TLM2 socket direction, width, or bridge ownership differs");
        return std::nullopt;
    }
    if (transactions_.size() >= limits_.max_transactions) {
        report_error(diagnostics, SystemCKernelTlm2Code::resource,
            "SystemC TLM2 transaction registry exceeds its governed limit");
        return std::nullopt;
    }
    const auto transaction
        = make_systemc_transaction_id(endpoint, sequence, diagnostics);
    if (!transaction
        || find_transaction(transactions_, *transaction) != nullptr) {
        report_error(diagnostics, SystemCKernelTlm2Code::lifecycle,
            "SystemC TLM2 transaction identity is invalid or repeated");
        return std::nullopt;
    }
    SystemCKernelTlm2Transaction record;
    record.transaction = *transaction;
    record.endpoint = endpoint;
    record.peer = peer;
    record.sequence = sequence;
    record.operation = operation;
    record.phase = phase;
    record.time_fs = time_fs;
    record.delta = delta;
    record.explicit_bridge = source->explicit_bridge;
    record.payload = std::move(payload);
    if (!validate_systemc_kernel_tlm2_transaction(
            record, limits_, diagnostics)) {
        return std::nullopt;
    }
    transactions_.push_back(std::move(record));
    return transaction;
}

bool SystemCKernelTlm2Registry::update(
    const SystemCTransactionId transaction, const SystemCKernelTlm2State state,
    const SystemCKernelTlm2Phase phase, const SystemCKernelTlm2Sync sync,
    SystemCKernelTlm2Payload payload, const std::uint64_t delay_fs,
    const std::uint32_t transferred, std::optional<SystemCKernelTlm2Dmi> dmi,
    diagnostic::Engine& diagnostics)
{
    auto* record = find_transaction(transactions_, transaction);
    if (record == nullptr || !known_state(state)
        || !valid_transition(record == nullptr ? SystemCKernelTlm2State::completed
                                               : record->state,
            state)) {
        return report_error(diagnostics, SystemCKernelTlm2Code::lifecycle,
            "SystemC TLM2 transaction transition is stale or illegal");
    }
    auto proposed = *record;
    proposed.state = state;
    proposed.phase = phase;
    proposed.sync = sync;
    proposed.delay_fs = delay_fs;
    proposed.transferred = transferred;
    proposed.payload = std::move(payload);
    proposed.dmi = std::move(dmi);
    if (!validate_systemc_kernel_tlm2_transaction(
            proposed, limits_, diagnostics)) {
        return false;
    }
    *record = std::move(proposed);
    return true;
}

const std::vector<SystemCKernelTlm2Endpoint>&
SystemCKernelTlm2Registry::endpoints() const noexcept
{
    return endpoints_;
}

const std::vector<SystemCKernelTlm2Transaction>&
SystemCKernelTlm2Registry::transactions() const noexcept
{
    return transactions_;
}

bool validate_systemc_kernel_tlm2_endpoint(
    const SystemCKernelTlm2Endpoint& endpoint,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return false;
    }
    if (!endpoint.endpoint.valid() || !known_socket(endpoint.kind)
        || endpoint.bus_width_bits == 0U || endpoint.peers.empty()
        || endpoint.peers.size() > limits.max_peers_per_endpoint
        || std::ranges::any_of(endpoint.peers, [&](const auto peer) {
               return !peer.valid() || peer == endpoint.endpoint;
           })) {
        return report_error(diagnostics, SystemCKernelTlm2Code::metadata,
            "SystemC TLM2 endpoint metadata or peers are invalid");
    }
    auto peers = endpoint.peers;
    std::ranges::sort(peers);
    if (std::ranges::adjacent_find(peers) != peers.end()) {
        return report_error(diagnostics, SystemCKernelTlm2Code::metadata,
            "SystemC TLM2 endpoint peers must be unique");
    }
    return true;
}

bool validate_systemc_kernel_tlm2_payload(const SystemCKernelTlm2Payload& payload,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics) || !known_command(payload.command)
        || !known_response(payload.response)
        || payload.data.size() > limits.max_data_bytes
        || payload.byte_enables.size() > limits.max_byte_enable_bytes
        || payload.extensions.size() > limits.max_extensions
        || (!payload.data.empty()
            && (payload.streaming_width == 0U
                || payload.streaming_width > payload.data.size()))) {
        return report_error(diagnostics, SystemCKernelTlm2Code::payload,
            "SystemC TLM2 generic payload metadata is invalid");
    }
    std::string_view previous;
    for (const auto& extension : payload.extensions) {
        if (extension.type_name.empty()
            || extension.type_name.size() > limits.max_extension_type_bytes
            || extension.bytes.size() > limits.max_extension_bytes
            || (!previous.empty() && previous >= extension.type_name)) {
            return report_error(diagnostics, SystemCKernelTlm2Code::resource,
                "SystemC TLM2 extensions are invalid, unordered, or over budget");
        }
        previous = extension.type_name;
    }
    return true;
}

bool validate_systemc_kernel_tlm2_transaction(
    const SystemCKernelTlm2Transaction& transaction,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)
        || !transaction.transaction.valid() || !transaction.endpoint.valid()
        || !transaction.peer.valid() || transaction.peer == transaction.endpoint
        || !transaction.sequence.valid() || !known_operation(transaction.operation)
        || !known_phase(transaction.phase) || !known_sync(transaction.sync)
        || !known_state(transaction.state)) {
        return report_error(diagnostics, SystemCKernelTlm2Code::metadata,
            "SystemC TLM2 transaction metadata is incomplete or unsupported");
    }
    const auto expected = make_systemc_transaction_id(
        transaction.endpoint, transaction.sequence, diagnostics);
    if (!expected || *expected != transaction.transaction) {
        return report_error(diagnostics, SystemCKernelTlm2Code::metadata,
            "SystemC TLM2 transaction identity is noncanonical");
    }
    if (!validate_systemc_kernel_tlm2_payload(
            transaction.payload, limits, diagnostics)
        || !valid_operation_shape(transaction, diagnostics)) {
        return false;
    }
    return !transaction.dmi || valid_dmi(*transaction.dmi, diagnostics);
}

std::optional<std::vector<std::byte>>
serialize_systemc_kernel_tlm2_transaction(
    const SystemCKernelTlm2Transaction& transaction,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_systemc_kernel_tlm2_transaction(
            transaction, limits, diagnostics)) {
        return std::nullopt;
    }
    if (!transaction.explicit_bridge) {
        report_error(diagnostics, SystemCKernelTlm2Code::lifecycle,
            "Native in-island SystemC TLM2 traffic must not be serialized");
        return std::nullopt;
    }
    const auto extensions_size = extension_bytes(transaction.payload);
    if (!extensions_size
        || transaction.payload.data.size()
            > std::numeric_limits<std::size_t>::max()
                - transaction.payload.byte_enables.size()
        || transaction.payload.data.size()
                + transaction.payload.byte_enables.size()
            > std::numeric_limits<std::size_t>::max() - *extensions_size
        || transaction.payload.data.size()
            > std::numeric_limits<std::uint32_t>::max()
        || transaction.payload.byte_enables.size()
            > std::numeric_limits<std::uint32_t>::max()) {
        report_error(diagnostics, SystemCKernelTlm2Code::resource,
            "SystemC TLM2 transaction encoding exceeds its governed limit");
        return std::nullopt;
    }
    const auto variable_size = transaction.payload.data.size()
        + transaction.payload.byte_enables.size() + *extensions_size;
    if (variable_size > limits.max_encoded_bytes - kHeaderBytes) {
        report_error(diagnostics, SystemCKernelTlm2Code::resource,
            "SystemC TLM2 transaction encoding exceeds its governed limit");
        return std::nullopt;
    }
    Writer writer { kHeaderBytes + variable_size };
    writer.raw(kMagic);
    writer.u32(kSystemCKernelTlm2Version);
    writer.u8(static_cast<std::uint8_t>(transaction.operation));
    writer.u8(static_cast<std::uint8_t>(transaction.phase));
    writer.u8(static_cast<std::uint8_t>(transaction.sync));
    writer.u8(static_cast<std::uint8_t>(transaction.state));
    writer.u8(static_cast<std::uint8_t>(transaction.payload.command));
    writer.u8(static_cast<std::uint8_t>(transaction.payload.response));
    writer.u8(static_cast<std::uint8_t>(1U
        | (transaction.payload.dmi_allowed ? 2U : 0U)
        | (transaction.dmi ? 4U : 0U)));
    writer.u8(0U);
    writer.id(transaction.transaction);
    writer.id(transaction.endpoint);
    writer.id(transaction.peer);
    writer.u64(transaction.sequence.value);
    writer.u64(transaction.time_fs);
    writer.u64(transaction.delta);
    writer.u64(transaction.delay_fs);
    writer.u64(transaction.payload.address);
    writer.u32(transaction.payload.streaming_width);
    writer.u32(transaction.transferred);
    writer.u32(static_cast<std::uint32_t>(transaction.payload.data.size()));
    writer.u32(
        static_cast<std::uint32_t>(transaction.payload.byte_enables.size()));
    writer.u32(static_cast<std::uint32_t>(transaction.payload.extensions.size()));
    writer.u64(transaction.dmi ? transaction.dmi->start_address : 0U);
    writer.u64(transaction.dmi ? transaction.dmi->end_address : 0U);
    writer.u64(transaction.dmi ? transaction.dmi->read_latency_fs : 0U);
    writer.u64(transaction.dmi ? transaction.dmi->write_latency_fs : 0U);
    writer.u8(static_cast<std::uint8_t>(transaction.dmi
            ? transaction.dmi->access
            : SystemCKernelTlm2DmiAccess::none));
    writer.u8(0U);
    writer.u8(0U);
    writer.u8(0U);
    writer.raw(transaction.payload.data);
    writer.raw(transaction.payload.byte_enables);
    for (const auto& extension : transaction.payload.extensions) {
        writer.u32(static_cast<std::uint32_t>(extension.type_name.size()));
        writer.u32(static_cast<std::uint32_t>(extension.bytes.size()));
        writer.text(extension.type_name);
        writer.raw(extension.bytes);
    }
    return std::move(writer).take();
}

std::optional<SystemCKernelTlm2Transaction>
deserialize_systemc_kernel_tlm2_transaction(
    const std::span<const std::byte> bytes,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return std::nullopt;
    }
    if (bytes.size() < kHeaderBytes || bytes.size() > limits.max_encoded_bytes) {
        report_error(diagnostics, SystemCKernelTlm2Code::resource,
            "SystemC TLM2 transaction bytes are truncated or over budget");
        return std::nullopt;
    }
    Reader reader { bytes };
    std::span<const std::byte> magic;
    std::uint32_t version { };
    std::uint8_t operation { };
    std::uint8_t phase { };
    std::uint8_t sync { };
    std::uint8_t state { };
    std::uint8_t command { };
    std::uint8_t response { };
    std::uint8_t flags { };
    std::uint8_t reserved0 { };
    std::uint32_t data_size { };
    std::uint32_t byte_enable_size { };
    std::uint32_t extension_count { };
    SystemCKernelTlm2Transaction transaction;
    SystemCKernelTlm2Dmi dmi;
    std::uint8_t access { };
    std::uint8_t reserved1 { };
    std::uint8_t reserved2 { };
    std::uint8_t reserved3 { };
    if (!reader.raw(kMagic.size(), magic) || !std::ranges::equal(magic, kMagic)
        || !reader.u32(version) || !reader.u8(operation) || !reader.u8(phase)
        || !reader.u8(sync) || !reader.u8(state) || !reader.u8(command)
        || !reader.u8(response) || !reader.u8(flags) || !reader.u8(reserved0)
        || !reader.id(transaction.transaction) || !reader.id(transaction.endpoint)
        || !reader.id(transaction.peer) || !reader.u64(transaction.sequence.value)
        || !reader.u64(transaction.time_fs) || !reader.u64(transaction.delta)
        || !reader.u64(transaction.delay_fs)
        || !reader.u64(transaction.payload.address)
        || !reader.u32(transaction.payload.streaming_width)
        || !reader.u32(transaction.transferred) || !reader.u32(data_size)
        || !reader.u32(byte_enable_size) || !reader.u32(extension_count)
        || !reader.u64(dmi.start_address) || !reader.u64(dmi.end_address)
        || !reader.u64(dmi.read_latency_fs)
        || !reader.u64(dmi.write_latency_fs) || !reader.u8(access)
        || !reader.u8(reserved1) || !reader.u8(reserved2)
        || !reader.u8(reserved3) || version != kSystemCKernelTlm2Version
        || (flags & 0xf8U) != 0U || (flags & 1U) == 0U || reserved0 != 0U
        || reserved1 != 0U || reserved2 != 0U || reserved3 != 0U
        || data_size > reader.remaining()
        || byte_enable_size > reader.remaining() - data_size
        || extension_count > limits.max_extensions) {
        report_error(diagnostics, SystemCKernelTlm2Code::payload,
            "SystemC TLM2 transaction encoding is malformed or noncanonical");
        return std::nullopt;
    }
    transaction.operation = static_cast<SystemCKernelTlm2Operation>(operation);
    transaction.phase = static_cast<SystemCKernelTlm2Phase>(phase);
    transaction.sync = static_cast<SystemCKernelTlm2Sync>(sync);
    transaction.state = static_cast<SystemCKernelTlm2State>(state);
    transaction.explicit_bridge = true;
    transaction.payload.command = static_cast<SystemCKernelTlm2Command>(command);
    transaction.payload.response
        = static_cast<SystemCKernelTlm2Response>(response);
    transaction.payload.dmi_allowed = (flags & 2U) != 0U;
    if (!reader.bytes(data_size, transaction.payload.data)
        || !reader.bytes(byte_enable_size, transaction.payload.byte_enables)) {
        report_error(diagnostics, SystemCKernelTlm2Code::payload,
            "SystemC TLM2 data or byte-enable payload is truncated");
        return std::nullopt;
    }
    if (!read_extensions(reader, extension_count, limits, transaction.payload,
            diagnostics)) {
        return std::nullopt;
    }
    if ((flags & 4U) != 0U) {
        dmi.access = static_cast<SystemCKernelTlm2DmiAccess>(access);
        transaction.dmi = dmi;
    } else if (dmi.start_address != 0U || dmi.end_address != 0U
        || dmi.read_latency_fs != 0U || dmi.write_latency_fs != 0U
        || access != 0U) {
        report_error(diagnostics, SystemCKernelTlm2Code::payload,
            "SystemC TLM2 absent DMI metadata is noncanonical");
        return std::nullopt;
    }
    if (reader.remaining() != 0U
        || !validate_systemc_kernel_tlm2_transaction(
            transaction, limits, diagnostics)) {
        if (!diagnostics.has_error()) {
            report_error(diagnostics, SystemCKernelTlm2Code::payload,
                "SystemC TLM2 transaction has trailing bytes");
        }
        return std::nullopt;
    }
    return transaction;
}

const char* systemc_kernel_tlm2_diagnostic_code(
    const SystemCKernelTlm2Code code) noexcept
{
    switch (code) {
    case SystemCKernelTlm2Code::none:
        return "";
    case SystemCKernelTlm2Code::metadata:
        return "FSIM-SC-U001";
    case SystemCKernelTlm2Code::lifecycle:
        return "FSIM-SC-U002";
    case SystemCKernelTlm2Code::payload:
        return "FSIM-SC-U003";
    case SystemCKernelTlm2Code::resource:
        return "FSIM-SC-U004";
    }
    return "FSIM-SC-U001";
}

} // namespace fsim::systemc
