// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_loopback.hpp"
#include "fsim/systemc/kernel_backend_synchronization.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fsim::systemc;

struct SessionIds {
    SystemCIslandId island;
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCEndpointId input;
    SystemCEndpointId output;
};

struct LiveSession {
    std::unique_ptr<SystemCKernelBackend> backend;
    SessionIds ids;
    SystemCKernelHostLanguage host_language;
};

SessionIds make_ids(const std::string& identity, const std::string& root,
    const SystemCKernelProtocolLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const auto island = make_systemc_island_id(identity, limits, diagnostics);
    const auto hierarchy = island
        ? make_systemc_hierarchy_id(
              *island, "synchronized-roots", limits, diagnostics)
        : std::nullopt;
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
    assert(island && hierarchy && object && input && output);
    assert(diagnostics.diagnostics().empty());
    return { *island, *hierarchy, *object, *input, *output };
}

std::vector<std::byte> create_session_payload(const std::string& identity,
    const std::filesystem::path& plugin,
    const SystemCKernelSessionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const auto result = serialize_systemc_create_session_payload(
        { identity, plugin.string(), 1U }, limits, diagnostics);
    assert(result);
    return *result;
}

std::vector<std::byte> create_object_payload(const std::string& root,
    const SystemCKernelSessionLimits& limits)
{
    SystemCKernelCreateObjectPayload payload;
    payload.hierarchy_path = "synchronized-roots";
    payload.object_path = root;
    payload.factory = "execution_root";
    payload.instance = root;
    fsim::diagnostic::Engine diagnostics;
    const auto result = serialize_systemc_create_object_payload(
        payload, limits, diagnostics);
    assert(result);
    return *result;
}

std::vector<std::byte> binding_payload(const std::string& endpoint,
    const std::string& channel, const SystemCKernelSessionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const auto result = serialize_systemc_bind_endpoint_payload(
        { endpoint, channel }, limits, diagnostics);
    assert(result);
    return *result;
}

void lifecycle_exchange(SystemCKernelBackend& backend,
    const SystemCKernelOperation operation, const std::uint64_t sequence,
    const SessionIds& ids, const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    std::vector<std::byte> payload = { },
    const SystemCEndpointId endpoint = { })
{
    SystemCKernelMessage request;
    request.header.operation = operation;
    request.header.direction = SystemCKernelMessageDirection::request;
    request.header.sequence = { sequence };
    request.header.island = ids.island;
    if (operation == SystemCKernelOperation::create_object
        || endpoint.valid()) {
        request.header.hierarchy = ids.hierarchy;
        request.header.object = ids.object;
    }
    request.header.endpoint = endpoint;
    request.payload = std::move(payload);
    fsim::diagnostic::Engine diagnostics;
    const auto encoded = serialize_systemc_kernel_message(
        request, protocol_limits, diagnostics);
    assert(encoded);
    const auto transport = backend.exchange(*encoded);
    assert(transport.status == SystemCKernelTransportStatus::ok);
    const auto response = deserialize_systemc_kernel_message(
        transport.bytes, protocol_limits, diagnostics);
    assert(response);
    assert(response->header.direction
        == SystemCKernelMessageDirection::response);
    assert(response->header.status == SystemCKernelMessageStatus::ok);
    assert(response->header.operation == operation);
    assert(response->header.correlation == request.header.sequence);
    const auto receipt = deserialize_systemc_lifecycle_receipt(
        response->payload, session_limits, diagnostics);
    assert(receipt && receipt->code == SystemCKernelLifecycleCode::none);
}

LiveSession make_live_session(const std::filesystem::path& plugin,
    const std::string& identity, const std::string& root,
    const SystemCKernelHostLanguage host_language, const bool loopback,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits)
{
    fsim::diagnostic::Engine diagnostics;
    std::unique_ptr<SystemCKernelBackend> backend;
    if (loopback) {
        backend = make_systemc_kernel_loopback_session_backend(protocol_limits,
            session_limits, execution_limits, { }, diagnostics);
    } else {
        backend = make_systemc_kernel_session_backend(protocol_limits,
            session_limits, execution_limits, diagnostics);
    }
    assert(backend && diagnostics.diagnostics().empty());
    const auto ids = make_ids(identity, root, protocol_limits);
    lifecycle_exchange(*backend, SystemCKernelOperation::create_session, 1U,
        ids, protocol_limits, session_limits,
        create_session_payload(identity, plugin, session_limits));
    lifecycle_exchange(*backend, SystemCKernelOperation::create_object, 2U,
        ids, protocol_limits, session_limits,
        create_object_payload(root, session_limits));
    lifecycle_exchange(*backend, SystemCKernelOperation::bind_endpoint, 3U,
        ids, protocol_limits, session_limits,
        binding_payload(
            root + ".input", root + ".input_channel", session_limits),
        ids.input);
    lifecycle_exchange(*backend, SystemCKernelOperation::bind_endpoint, 4U,
        ids, protocol_limits, session_limits,
        binding_payload(
            root + ".output", root + ".output_channel", session_limits),
        ids.output);
    lifecycle_exchange(*backend, SystemCKernelOperation::elaborate, 5U, ids,
        protocol_limits, session_limits);
    lifecycle_exchange(*backend, SystemCKernelOperation::start, 6U, ids,
        protocol_limits, session_limits);
    return { std::move(backend), ids, host_language };
}

SystemCKernelSynchronizedIsland registration(const LiveSession& session)
{
    return { session.ids.island, session.host_language, { 7U }, 0U,
        { { session.ids.hierarchy, session.ids.object,
            session.ids.output } } };
}

SystemCKernelSynchronizationInput input(
    const LiveSession& session, const std::uint64_t value)
{
    return { session.ids.island,
        { session.ids.hierarchy, session.ids.object, session.ids.input },
        { value, 32U, false } };
}

bool has_code(const fsim::diagnostic::Engine& diagnostics,
    const std::string& code)
{
    return std::ranges::any_of(diagnostics.diagnostics(),
        [&](const auto& item) { return item.code == code; });
}

struct DisconnectState {
    std::size_t exchanges { };
    std::size_t closes { };
};

class DisconnectBackend final : public SystemCKernelBackend {
public:
    explicit DisconnectBackend(std::shared_ptr<DisconnectState> state)
        : state_ { std::move(state) }
    {
    }

    [[nodiscard]] SystemCKernelTransportResult exchange(
        std::span<const std::byte>) noexcept override
    {
        ++state_->exchanges;
        return { SystemCKernelTransportStatus::disconnected, { } };
    }

    void close() noexcept override { ++state_->closes; }

private:
    std::shared_ptr<DisconnectState> state_;
};

void audit_containment_and_factory_recovery(
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelExecutionLimits& execution_limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto invalid_limits = SystemCKernelSynchronizationLimits { };
    invalid_limits.max_batches = 0U;
    assert(!make_systemc_kernel_synchronizer(protocol_limits, execution_limits,
        invalid_limits, diagnostics));
    assert(has_code(diagnostics, "FSIM-SC-N003"));

    diagnostics = { };
    auto synchronizer = make_systemc_kernel_synchronizer(protocol_limits,
        execution_limits, { }, diagnostics);
    assert(synchronizer);
    const auto island = make_ids(
        "disconnect-island", "disconnect_root", protocol_limits);
    auto state = std::make_shared<DisconnectState>();
    assert(synchronizer->attach_island(
        { island.island, SystemCKernelHostLanguage::verilog, { 1U }, 0U,
            { } },
        std::make_unique<DisconnectBackend>(state), diagnostics));
    assert(!synchronizer->synchronize({ 0U, 0U }, { }, diagnostics));
    assert(has_code(diagnostics, "FSIM-SC-N004"));
    assert(synchronizer->failed());
    assert(synchronizer->island_count() == 0U);
    assert(state->exchanges == 1U && state->closes == 1U);

    diagnostics = { };
    auto recovered = make_systemc_kernel_synchronizer(protocol_limits,
        execution_limits, { }, diagnostics);
    assert(recovered && !recovered->failed());
    assert(recovered->island_count() == 0U);
}

void require_stage_order(const SystemCKernelSynchronizationReceipt& receipt,
    const std::size_t inputs, const std::size_t islands,
    const std::size_t outputs)
{
    assert(receipt.crossings.size() == inputs + islands + outputs);
    for (std::size_t index = 0U; index < inputs; ++index) {
        assert(receipt.crossings[index].stage
            == SystemCKernelCrossingStage::input_batch);
    }
    for (std::size_t index = inputs; index < inputs + islands; ++index) {
        assert(receipt.crossings[index].stage
            == SystemCKernelCrossingStage::kernel_quiescent);
        assert(receipt.crossings[index].execution_order.region
            == SystemCAccelleraRegion::quiescent);
    }
    for (std::size_t index = inputs + islands;
        index < receipt.crossings.size(); ++index) {
        assert(receipt.crossings[index].stage
            == SystemCKernelCrossingStage::output_batch);
    }
}

void audit_mixed_root_synchronization(const std::filesystem::path& plugin,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits)
{
    auto sv = make_live_session(plugin, "sv-systemc-island", "sv_root",
        SystemCKernelHostLanguage::system_verilog, false, protocol_limits,
        session_limits, execution_limits);
    auto vhdl = make_live_session(plugin, "vhdl-systemc-island", "vhdl_root",
        SystemCKernelHostLanguage::vhdl, true, protocol_limits, session_limits,
        execution_limits);
    assert(systemc_kernel_backend_live_contexts() == 2U);

    fsim::diagnostic::Engine diagnostics;
    SystemCKernelSynchronizationLimits limits;
    limits.max_inputs_per_batch = 2U;
    auto synchronizer = make_systemc_kernel_synchronizer(
        protocol_limits, execution_limits, limits, diagnostics);
    assert(synchronizer);
    assert(synchronizer->attach_island(
        registration(vhdl), std::move(vhdl.backend), diagnostics));
    assert(synchronizer->attach_island(
        registration(sv), std::move(sv.backend), diagnostics));
    assert(synchronizer->island_count() == 2U);

    const std::vector initial_inputs { input(vhdl, 7U), input(sv, 4U) };
    const auto first
        = synchronizer->synchronize({ 0U, 0U }, initial_inputs, diagnostics);
    if (!first) {
        for (const auto& item : diagnostics.diagnostics()) {
            std::cerr << item.code << ": " << item.message << '\n';
        }
    }
    assert(first && diagnostics.diagnostics().empty());
    require_stage_order(*first, 2U, 2U, 2U);
    assert(first->dirty_outputs.size() == 2U);
    assert(std::ranges::is_sorted(first->dirty_outputs, { },
        &SystemCKernelExecutionSample::order));
    for (const auto& sample : first->dirty_outputs) {
        assert(sample.dirty);
        assert(sample.order.region == SystemCAccelleraRegion::update);
        if (sample.endpoint == sv.ids.output) {
            assert(sample.value.bits == 13U);
        } else {
            assert(sample.endpoint == vhdl.ids.output);
            assert(sample.value.bits == 22U);
        }
    }
    assert(first->future_activity);
    assert(first->next_activity_time_fs == 5'000'000U);

    diagnostics = { };
    const auto empty_delta
        = synchronizer->synchronize({ 0U, 1U }, { }, diagnostics);
    assert(empty_delta && empty_delta->dirty_outputs.empty());
    require_stage_order(*empty_delta, 0U, 2U, 2U);
    assert(!synchronizer->synchronize({ 0U, 1U }, { }, diagnostics));
    assert(has_code(diagnostics, "FSIM-SC-N001"));
    assert(!synchronizer->failed());

    diagnostics = { };
    const std::vector excessive_inputs {
        input(sv, 8U), input(vhdl, 9U), input(sv, 10U)
    };
    assert(!synchronizer->synchronize(
        { 0U, 2U }, excessive_inputs, diagnostics));
    assert(has_code(diagnostics, "FSIM-SC-N003"));
    assert(!synchronizer->failed());

    diagnostics = { };
    const std::vector duplicate_inputs { input(sv, 8U), input(sv, 9U) };
    assert(!synchronizer->synchronize(
        { 0U, 2U }, duplicate_inputs, diagnostics));
    assert(has_code(diagnostics, "FSIM-SC-N002"));
    assert(!synchronizer->failed());

    diagnostics = { };
    const std::vector next_input { input(sv, 8U) };
    const auto next
        = synchronizer->synchronize({ 0U, 2U }, next_input, diagnostics);
    assert(next && next->dirty_outputs.size() == 1U);
    assert(next->dirty_outputs.front().endpoint == sv.ids.output);
    assert(next->dirty_outputs.front().value.bits == 25U);
    require_stage_order(*next, 1U, 2U, 2U);

    diagnostics = { };
    const auto timed
        = synchronizer->synchronize({ 5'000'000U, 0U }, { }, diagnostics);
    if (!timed) {
        for (const auto& item : diagnostics.diagnostics()) {
            std::cerr << item.code << ": " << item.message << '\n';
        }
    }
    assert(timed && timed->point.time_fs == 5'000'000U);
    assert(timed->dirty_outputs.empty());
    assert(timed->crossings.size() == 6U);
    assert(timed->crossings[0].stage
        == SystemCKernelCrossingStage::kernel_arrival);
    assert(timed->crossings[1].stage
        == SystemCKernelCrossingStage::kernel_arrival);
    assert(timed->crossings[2].stage
        == SystemCKernelCrossingStage::kernel_quiescent);
    assert(timed->crossings[3].stage
        == SystemCKernelCrossingStage::kernel_quiescent);
    assert(timed->crossings[4].stage
        == SystemCKernelCrossingStage::output_batch);
    assert(timed->crossings[5].stage
        == SystemCKernelCrossingStage::output_batch);
    assert(timed->future_activity);
    assert(timed->next_activity_time_fs == 10'000'000U);
    synchronizer->close();
    assert(synchronizer->island_count() == 0U);
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
    audit_containment_and_factory_recovery(
        protocol_limits, execution_limits);
    audit_mixed_root_synchronization(
        plugin, protocol_limits, session_limits, execution_limits);
    assert(*systemc_kernel_synchronization_diagnostic_code(
               SystemCKernelSynchronizationCode::order)
        != '\0');
    assert(*systemc_kernel_synchronization_diagnostic_code(
               SystemCKernelSynchronizationCode::payload)
        != '\0');
    assert(*systemc_kernel_synchronization_diagnostic_code(
               SystemCKernelSynchronizationCode::resource)
        != '\0');
    assert(*systemc_kernel_synchronization_diagnostic_code(
               SystemCKernelSynchronizationCode::transport)
        != '\0');
    return 0;
}
