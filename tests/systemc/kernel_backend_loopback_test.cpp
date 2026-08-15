// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_loopback.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fsim::systemc;

enum class FakeMode {
    echo,
    alternate_response_sequence,
    disconnect,
    malformed,
    wrong_correlation,
};

struct FakeState {
    std::size_t exchanges { };
    std::size_t closes { };
    std::vector<std::byte> last_response;
};

class FakeBackend final : public SystemCKernelBackend {
public:
    FakeBackend(SystemCKernelProtocolLimits limits, const FakeMode mode,
        std::shared_ptr<FakeState> state)
        : limits_ { limits }
        , mode_ { mode }
        , state_ { std::move(state) }
    {
    }

    [[nodiscard]] SystemCKernelTransportResult exchange(
        const std::span<const std::byte> request_bytes) noexcept override
    {
        ++state_->exchanges;
        if (mode_ == FakeMode::disconnect) {
            return { SystemCKernelTransportStatus::disconnected, { } };
        }
        if (mode_ == FakeMode::malformed) {
            return { SystemCKernelTransportStatus::ok,
                { std::byte { 0x42U } } };
        }
        fsim::diagnostic::Engine diagnostics;
        const auto request = deserialize_systemc_kernel_message(
            request_bytes, limits_, diagnostics);
        if (!request) {
            return { SystemCKernelTransportStatus::rejected, { } };
        }
        SystemCKernelMessage response = *request;
        response.header.direction = SystemCKernelMessageDirection::response;
        response.header.status = SystemCKernelMessageStatus::ok;
        response.header.correlation = request->header.sequence;
        if (mode_ == FakeMode::alternate_response_sequence) {
            response.header.sequence.value += 100U;
        }
        if (mode_ == FakeMode::wrong_correlation) {
            response.header.correlation.value += 1U;
        }
        diagnostics = { };
        auto encoded = serialize_systemc_kernel_message(
            response, limits_, diagnostics);
        if (!encoded) {
            return { SystemCKernelTransportStatus::disconnected, { } };
        }
        state_->last_response = *encoded;
        return { SystemCKernelTransportStatus::ok, std::move(*encoded) };
    }

    void close() noexcept override { ++state_->closes; }

private:
    SystemCKernelProtocolLimits limits_;
    FakeMode mode_ { FakeMode::echo };
    std::shared_ptr<FakeState> state_;
};

SystemCIslandId island_id(const std::string& identity,
    const SystemCKernelProtocolLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const auto result = make_systemc_island_id(identity, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return *result;
}

std::vector<std::byte> request_bytes(const SystemCKernelOperation operation,
    const SystemCSequenceId sequence,
    const SystemCKernelProtocolLimits& limits,
    const SystemCIslandId island = { }, const std::uint32_t flags = 0U,
    std::vector<std::byte> payload = { },
    const SystemCHierarchyId hierarchy = { },
    const SystemCObjectId object = { },
    const SystemCEndpointId endpoint = { })
{
    SystemCKernelMessage request;
    request.header.operation = operation;
    request.header.direction = SystemCKernelMessageDirection::request;
    request.header.sequence = sequence;
    request.header.flags = flags;
    request.header.island = island;
    request.header.hierarchy = hierarchy;
    request.header.object = object;
    request.header.endpoint = endpoint;
    request.payload = std::move(payload);
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_kernel_message(request, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

std::unique_ptr<SystemCKernelLoopbackBackend> fake_loopback(
    const FakeMode mode, const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelLoopbackLimits& loopback_limits,
    const std::shared_ptr<FakeState>& state)
{
    fsim::diagnostic::Engine diagnostics;
    auto peer = std::make_unique<FakeBackend>(protocol_limits, mode, state);
    auto result = make_systemc_kernel_loopback_backend(std::move(peer),
        protocol_limits, loopback_limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return result;
}

void audit_loopback_transport(const SystemCKernelProtocolLimits& protocol_limits)
{
    const SystemCKernelLoopbackLimits loopback_limits;
    auto state = std::make_shared<FakeState>();
    auto loopback = fake_loopback(
        FakeMode::echo, protocol_limits, loopback_limits, state);
    assert(systemc_kernel_loopback_live_transports() == 1U);
    const auto replayable = static_cast<std::uint32_t>(
        SystemCKernelMessageFlag::replayable);
    auto first = request_bytes(SystemCKernelOperation::handshake, { 10U },
        protocol_limits, { }, replayable,
        { std::byte { 1U }, std::byte { 2U } });
    const auto response = loopback->exchange(first);
    assert(response.status == SystemCKernelTransportStatus::ok);
    assert(response.bytes == state->last_response);
    assert(state->exchanges == 1U);
    const auto replay = loopback->exchange(first);
    assert(replay.status == SystemCKernelTransportStatus::ok);
    assert(replay.bytes == response.bytes);
    assert(state->exchanges == 1U);
    assert(loopback->stats().replayed == 1U);

    auto changed = request_bytes(SystemCKernelOperation::handshake, { 10U },
        protocol_limits, { }, replayable, { std::byte { 3U } });
    assert(loopback->exchange(changed).status
        == SystemCKernelTransportStatus::rejected);
    assert(loopback->last_code() == SystemCKernelLoopbackCode::order);
    auto skipped = request_bytes(SystemCKernelOperation::handshake, { 12U },
        protocol_limits);
    assert(loopback->exchange(skipped).status
        == SystemCKernelTransportStatus::rejected);
    auto stale = request_bytes(SystemCKernelOperation::handshake, { 9U },
        protocol_limits);
    assert(loopback->exchange(stale).status
        == SystemCKernelTransportStatus::rejected);
    auto next = request_bytes(SystemCKernelOperation::handshake, { 11U },
        protocol_limits);
    assert(loopback->exchange(next).status
        == SystemCKernelTransportStatus::ok);
    assert(state->exchanges == 2U);
    assert(loopback->stats().rejected == 3U);
    loopback.reset();
    assert(systemc_kernel_loopback_live_transports() == 0U);
    assert(state->closes == 1U);

    auto alternate_state = std::make_shared<FakeState>();
    auto alternate = fake_loopback(FakeMode::alternate_response_sequence,
        protocol_limits, loopback_limits, alternate_state);
    const auto correlated = alternate->exchange(first);
    assert(correlated.status == SystemCKernelTransportStatus::ok);
    fsim::diagnostic::Engine diagnostics;
    const auto message = deserialize_systemc_kernel_message(
        correlated.bytes, protocol_limits, diagnostics);
    assert(message && message->header.sequence.value == 110U);
    assert(message->header.correlation.value == 10U);
}

void audit_bounds_and_eviction(
    const SystemCKernelProtocolLimits& protocol_limits)
{
    SystemCKernelLoopbackLimits limits;
    limits.max_replay_entries = 1U;
    limits.max_forwarded_exchanges = 2U;
    auto state = std::make_shared<FakeState>();
    auto loopback = fake_loopback(
        FakeMode::echo, protocol_limits, limits, state);
    const auto replayable = static_cast<std::uint32_t>(
        SystemCKernelMessageFlag::replayable);
    auto first = request_bytes(SystemCKernelOperation::handshake, { 1U },
        protocol_limits, { }, replayable);
    auto second = request_bytes(SystemCKernelOperation::handshake, { 2U },
        protocol_limits, { }, replayable);
    assert(loopback->exchange(first).status == SystemCKernelTransportStatus::ok);
    assert(loopback->exchange(second).status == SystemCKernelTransportStatus::ok);
    assert(loopback->stats().evicted == 1U);
    assert(loopback->stats().cached == 1U);
    assert(loopback->exchange(first).status
        == SystemCKernelTransportStatus::rejected);
    auto third = request_bytes(SystemCKernelOperation::handshake, { 3U },
        protocol_limits);
    assert(loopback->exchange(third).status
        == SystemCKernelTransportStatus::rejected);
    assert(loopback->last_code() == SystemCKernelLoopbackCode::resource);

    fsim::diagnostic::Engine diagnostics;
    auto peer_state = std::make_shared<FakeState>();
    auto peer = std::make_unique<FakeBackend>(
        protocol_limits, FakeMode::echo, peer_state);
    SystemCKernelLoopbackLimits invalid = limits;
    invalid.max_replay_entries = 0U;
    assert(!make_systemc_kernel_loopback_backend(std::move(peer),
        protocol_limits, invalid, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-L003");
}

void audit_failure_containment(
    const SystemCKernelProtocolLimits& protocol_limits)
{
    const SystemCKernelLoopbackLimits limits;
    const auto request = request_bytes(
        SystemCKernelOperation::handshake, { 1U }, protocol_limits);
    for (const auto mode : { FakeMode::disconnect, FakeMode::malformed,
             FakeMode::wrong_correlation }) {
        auto state = std::make_shared<FakeState>();
        auto loopback = fake_loopback(mode, protocol_limits, limits, state);
        assert(loopback->exchange(request).status
            == SystemCKernelTransportStatus::disconnected);
        const auto expected = mode == FakeMode::disconnect
            ? SystemCKernelLoopbackCode::peer
            : SystemCKernelLoopbackCode::response;
        assert(loopback->last_code() == expected);
        assert(loopback->stats().disconnected == 1U);
        assert(loopback->exchange(request).status
            == SystemCKernelTransportStatus::disconnected);
        assert(state->closes == 1U);
    }

    auto recovered_state = std::make_shared<FakeState>();
    auto recovered = fake_loopback(
        FakeMode::echo, protocol_limits, limits, recovered_state);
    assert(recovered->exchange(request).status
        == SystemCKernelTransportStatus::ok);
    assert(recovered->last_code() == SystemCKernelLoopbackCode::none);
}

struct SessionIds {
    SystemCIslandId island;
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCEndpointId input;
    SystemCEndpointId output;
};

SessionIds session_ids(const SystemCIslandId island, const std::string& root,
    const SystemCKernelProtocolLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const auto hierarchy = make_systemc_hierarchy_id(
        island, "loopback-roots", limits, diagnostics);
    const auto object = hierarchy
        ? make_systemc_object_id(*hierarchy, root, limits, diagnostics)
        : std::nullopt;
    const auto input = object
        ? make_systemc_endpoint_id(
              *object, root + ".input", limits, diagnostics)
        : std::nullopt;
    const auto output = object
        ? make_systemc_endpoint_id(
              *object, root + ".output", limits, diagnostics)
        : std::nullopt;
    assert(hierarchy && object && input && output);
    return { island, *hierarchy, *object, *input, *output };
}

std::vector<std::byte> encode_session_payload(const std::string& identity,
    const std::filesystem::path& plugin,
    const SystemCKernelSessionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_create_session_payload(
        { identity, plugin.string(), 1U }, limits, diagnostics);
    assert(result);
    return std::move(*result);
}

std::vector<std::byte> encode_object_payload(const std::string& root,
    const SystemCKernelSessionLimits& limits)
{
    SystemCKernelCreateObjectPayload payload;
    payload.hierarchy_path = "loopback-roots";
    payload.object_path = root;
    payload.factory = "execution_root";
    payload.instance = root;
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_create_object_payload(
        payload, limits, diagnostics);
    assert(result);
    return std::move(*result);
}

std::vector<std::byte> encode_binding(const std::string& endpoint,
    const std::string& channel, const SystemCKernelSessionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_bind_endpoint_payload(
        { endpoint, channel }, limits, diagnostics);
    assert(result);
    return std::move(*result);
}

std::vector<std::byte> encode_input(
    const SystemCKernelExecutionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_apply_inputs_payload(
        { { 7U, 32U, false } }, limits, diagnostics);
    assert(result);
    return std::move(*result);
}

std::vector<std::byte> encode_delta(
    const SystemCKernelExecutionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_advance_payload(
        { SystemCKernelAdvanceKind::delta, 0U }, limits, diagnostics);
    assert(result);
    return std::move(*result);
}

void compare_semantics(SystemCKernelBackend& direct,
    SystemCKernelLoopbackBackend& loopback,
    const std::vector<std::byte>& request,
    const SystemCKernelOperation operation,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits)
{
    const auto direct_transport = direct.exchange(request);
    const auto loopback_transport = loopback.exchange(request);
    assert(direct_transport.status == SystemCKernelTransportStatus::ok);
    assert(loopback_transport.status == SystemCKernelTransportStatus::ok);
    fsim::diagnostic::Engine diagnostics;
    const auto direct_message = deserialize_systemc_kernel_message(
        direct_transport.bytes, protocol_limits, diagnostics);
    diagnostics = { };
    const auto loopback_message = deserialize_systemc_kernel_message(
        loopback_transport.bytes, protocol_limits, diagnostics);
    assert(direct_message && loopback_message);
    assert(direct_message->header == loopback_message->header);
    if (operation <= SystemCKernelOperation::start
        || operation == SystemCKernelOperation::teardown) {
        diagnostics = { };
        auto direct_receipt = deserialize_systemc_lifecycle_receipt(
            direct_message->payload, session_limits, diagnostics);
        diagnostics = { };
        auto loopback_receipt = deserialize_systemc_lifecycle_receipt(
            loopback_message->payload, session_limits, diagnostics);
        assert(direct_receipt && loopback_receipt);
        direct_receipt->context_generation = 0U;
        loopback_receipt->context_generation = 0U;
        assert(*direct_receipt == *loopback_receipt);
    } else {
        diagnostics = { };
        const auto direct_receipt = deserialize_systemc_execution_receipt(
            direct_message->payload, execution_limits, diagnostics);
        diagnostics = { };
        const auto loopback_receipt = deserialize_systemc_execution_receipt(
            loopback_message->payload, execution_limits, diagnostics);
        assert(direct_receipt == loopback_receipt);
    }
}

void audit_real_session_equivalence(const std::filesystem::path& plugin,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto direct = make_systemc_kernel_session_backend(protocol_limits,
        session_limits, execution_limits, diagnostics);
    auto loopback = make_systemc_kernel_loopback_session_backend(protocol_limits,
        session_limits, execution_limits, { }, diagnostics);
    assert(direct && loopback && diagnostics.diagnostics().empty());
    const std::string identity = "loopback-session";
    const std::string root = "loopback_top";
    const auto island = island_id(identity, protocol_limits);
    const auto ids = session_ids(island, root, protocol_limits);
    const auto message = [&](const SystemCKernelOperation operation,
                             const std::uint64_t sequence,
                             std::vector<std::byte> payload = { },
                             const SystemCEndpointId endpoint = { }) {
        return request_bytes(operation, { sequence }, protocol_limits, island,
            0U, std::move(payload),
            operation == SystemCKernelOperation::create_object
                    || endpoint.valid()
                ? ids.hierarchy
                : SystemCHierarchyId { },
            operation == SystemCKernelOperation::create_object
                    || endpoint.valid()
                ? ids.object
                : SystemCObjectId { },
            endpoint);
    };
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::create_session, 1U,
            encode_session_payload(identity, plugin, session_limits)),
        SystemCKernelOperation::create_session, protocol_limits, session_limits,
        execution_limits);
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::create_object, 2U,
            encode_object_payload(root, session_limits)),
        SystemCKernelOperation::create_object, protocol_limits, session_limits,
        execution_limits);
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::bind_endpoint, 3U,
            encode_binding(root + ".input", root + ".input_channel",
                session_limits),
            ids.input),
        SystemCKernelOperation::bind_endpoint, protocol_limits, session_limits,
        execution_limits);
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::bind_endpoint, 4U,
            encode_binding(root + ".output", root + ".output_channel",
                session_limits),
            ids.output),
        SystemCKernelOperation::bind_endpoint, protocol_limits, session_limits,
        execution_limits);
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::elaborate, 5U),
        SystemCKernelOperation::elaborate, protocol_limits, session_limits,
        execution_limits);
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::start, 6U),
        SystemCKernelOperation::start, protocol_limits, session_limits,
        execution_limits);
    assert(systemc_kernel_backend_live_contexts() == 2U);
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::apply_inputs, 7U,
            encode_input(execution_limits), ids.input),
        SystemCKernelOperation::apply_inputs, protocol_limits, session_limits,
        execution_limits);
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::advance, 8U,
            encode_delta(execution_limits)),
        SystemCKernelOperation::advance, protocol_limits, session_limits,
        execution_limits);
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::snapshot, 9U),
        SystemCKernelOperation::snapshot, protocol_limits, session_limits,
        execution_limits);
    compare_semantics(*direct, *loopback,
        message(SystemCKernelOperation::teardown, 10U),
        SystemCKernelOperation::teardown, protocol_limits, session_limits,
        execution_limits);
    assert(loopback->stats().forwarded == 10U);
    assert(systemc_kernel_backend_live_contexts() == 0U);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path plugin = argv[1];
    const SystemCKernelProtocolLimits protocol_limits;
    const SystemCKernelSessionLimits session_limits;
    const SystemCKernelExecutionLimits execution_limits;
    assert(systemc_kernel_loopback_live_transports() == 0U);
    audit_loopback_transport(protocol_limits);
    audit_bounds_and_eviction(protocol_limits);
    audit_failure_containment(protocol_limits);
    audit_real_session_equivalence(
        plugin, protocol_limits, session_limits, execution_limits);
    assert(*systemc_kernel_loopback_diagnostic_code(
               SystemCKernelLoopbackCode::order)
        != '\0');
    assert(*systemc_kernel_loopback_diagnostic_code(
               SystemCKernelLoopbackCode::response)
        != '\0');
    assert(*systemc_kernel_loopback_diagnostic_code(
               SystemCKernelLoopbackCode::resource)
        != '\0');
    assert(*systemc_kernel_loopback_diagnostic_code(
               SystemCKernelLoopbackCode::peer)
        != '\0');
    return 0;
}
