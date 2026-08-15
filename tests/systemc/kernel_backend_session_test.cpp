// SPDX-License-Identifier: Apache-2.0
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/systemc/kernel_backend_session.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using fsim::systemc::SystemCEndpointId;
using fsim::systemc::SystemCHierarchyId;
using fsim::systemc::SystemCIslandId;
using fsim::systemc::SystemCKernelBindEndpointPayload;
using fsim::systemc::SystemCKernelConstructionParameter;
using fsim::systemc::SystemCKernelCreateObjectPayload;
using fsim::systemc::SystemCKernelCreateSessionPayload;
using fsim::systemc::SystemCKernelLifecycleCode;
using fsim::systemc::SystemCKernelLifecycleReceipt;
using fsim::systemc::SystemCKernelMessage;
using fsim::systemc::SystemCKernelMessageDirection;
using fsim::systemc::SystemCKernelMessageStatus;
using fsim::systemc::SystemCKernelOperation;
using fsim::systemc::SystemCKernelProtocolLimits;
using fsim::systemc::SystemCKernelSessionLimits;
using fsim::systemc::SystemCKernelSessionState;
using fsim::systemc::SystemCObjectId;
using fsim::systemc::SystemCSequenceId;

struct ObjectIds {
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCEndpointId endpoint;
};

struct Response {
    SystemCKernelMessage message;
    SystemCKernelLifecycleReceipt receipt;
};

SystemCIslandId island_id(const std::string& identity,
    const SystemCKernelProtocolLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const auto result = fsim::systemc::make_systemc_island_id(
        identity, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return *result;
}

ObjectIds object_ids(const SystemCIslandId island,
    const std::string& hierarchy_path, const std::string& object_path,
    const std::string& endpoint_path,
    const SystemCKernelProtocolLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const auto hierarchy = fsim::systemc::make_systemc_hierarchy_id(
        island, hierarchy_path, limits, diagnostics);
    assert(hierarchy);
    const auto object = fsim::systemc::make_systemc_object_id(
        *hierarchy, object_path, limits, diagnostics);
    assert(object);
    const auto endpoint = fsim::systemc::make_systemc_endpoint_id(
        *object, endpoint_path, limits, diagnostics);
    assert(endpoint && diagnostics.diagnostics().empty());
    return { *hierarchy, *object, *endpoint };
}

Response transact(fsim::systemc::SystemCKernelBackend& backend,
    const SystemCKernelOperation operation,
    const SystemCKernelMessageStatus expected_status,
    const SystemCSequenceId sequence,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCIslandId island = { },
    const SystemCHierarchyId hierarchy = { },
    const SystemCObjectId object = { },
    const SystemCEndpointId endpoint = { },
    std::vector<std::byte> payload = { })
{
    SystemCKernelMessage request;
    request.header.operation = operation;
    request.header.direction = SystemCKernelMessageDirection::request;
    request.header.sequence = sequence;
    request.header.island = island;
    request.header.hierarchy = hierarchy;
    request.header.object = object;
    request.header.endpoint = endpoint;
    request.payload = std::move(payload);

    fsim::diagnostic::Engine diagnostics;
    const auto encoded = fsim::systemc::serialize_systemc_kernel_message(
        request, protocol_limits, diagnostics);
    assert(encoded && diagnostics.diagnostics().empty());
    const auto transport = backend.exchange(*encoded);
    assert(transport.status
        == fsim::systemc::SystemCKernelTransportStatus::ok);
    diagnostics = { };
    auto response = fsim::systemc::deserialize_systemc_kernel_message(
        transport.bytes, protocol_limits, diagnostics);
    assert(response && diagnostics.diagnostics().empty());
    assert(response->header.operation == operation);
    assert(response->header.direction
        == SystemCKernelMessageDirection::response);
    assert(response->header.status == expected_status);
    assert(response->header.correlation == sequence);
    diagnostics = { };
    auto receipt = fsim::systemc::deserialize_systemc_lifecycle_receipt(
        response->payload, session_limits, diagnostics);
    assert(receipt && diagnostics.diagnostics().empty());
    if (expected_status == SystemCKernelMessageStatus::ok) {
        assert(receipt->code == SystemCKernelLifecycleCode::none);
    } else {
        assert(receipt->code != SystemCKernelLifecycleCode::none);
        assert(*fsim::systemc::systemc_kernel_lifecycle_diagnostic_code(
                   receipt->code)
            != '\0');
    }
    return { std::move(*response), std::move(*receipt) };
}

std::vector<std::byte> session_payload(const std::string& identity,
    const std::filesystem::path& plugin,
    const SystemCKernelSessionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = fsim::systemc::serialize_systemc_create_session_payload(
        { identity, plugin.string(), 1U }, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

std::vector<std::byte> object_payload(const std::string& hierarchy,
    const std::string& instance, const std::int64_t width,
    const bool fail, const SystemCKernelSessionLimits& limits)
{
    SystemCKernelCreateObjectPayload payload;
    payload.hierarchy_path = hierarchy;
    payload.object_path = instance;
    payload.factory = "session_root";
    payload.instance = instance;
    payload.parameters = {
        SystemCKernelConstructionParameter { "WIDTH", width },
        SystemCKernelConstructionParameter { "FAIL", fail ? 1 : 0 },
    };
    fsim::diagnostic::Engine diagnostics;
    auto result = fsim::systemc::serialize_systemc_create_object_payload(
        payload, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

std::vector<std::byte> binding_payload(const std::string& root,
    const std::string& interface_path,
    const SystemCKernelSessionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = fsim::systemc::serialize_systemc_bind_endpoint_payload(
        { root + ".leaf.input", interface_path }, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

void audit_payload_codecs(const SystemCKernelSessionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const SystemCKernelCreateSessionPayload session {
        "codec-island", "/tmp/codec-plugin", 10U
    };
    auto session_bytes = fsim::systemc::serialize_systemc_create_session_payload(
        session, limits, diagnostics);
    assert(session_bytes && diagnostics.diagnostics().empty());
    auto decoded_session = fsim::systemc::deserialize_systemc_create_session_payload(
        *session_bytes, limits, diagnostics);
    assert(decoded_session == session);
    session_bytes->pop_back();
    diagnostics = { };
    assert(!fsim::systemc::deserialize_systemc_create_session_payload(
        *session_bytes, limits, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-S002");

    SystemCKernelCreateObjectPayload object;
    object.hierarchy_path = "codec-roots";
    object.object_path = "codec_top";
    object.factory = "session_root";
    object.instance = "codec_top";
    object.parameters = { { "WIDTH", 17 }, { "FAIL", 0 } };
    diagnostics = { };
    auto object_bytes = fsim::systemc::serialize_systemc_create_object_payload(
        object, limits, diagnostics);
    assert(object_bytes);
    auto decoded_object = fsim::systemc::deserialize_systemc_create_object_payload(
        *object_bytes, limits, diagnostics);
    assert(decoded_object == object);
    object.parameters.push_back({ "WIDTH", 19 });
    diagnostics = { };
    assert(!fsim::systemc::serialize_systemc_create_object_payload(
        object, limits, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-S002");

    const SystemCKernelBindEndpointPayload binding {
        "codec_top.leaf.input", "codec_top.channel"
    };
    diagnostics = { };
    auto binding_bytes = fsim::systemc::serialize_systemc_bind_endpoint_payload(
        binding, limits, diagnostics);
    assert(binding_bytes);
    auto decoded_binding = fsim::systemc::deserialize_systemc_bind_endpoint_payload(
        *binding_bytes, limits, diagnostics);
    assert(decoded_binding == binding);
    binding_bytes->push_back(std::byte { 0U });
    diagnostics = { };
    assert(!fsim::systemc::deserialize_systemc_bind_endpoint_payload(
        *binding_bytes, limits, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-S002");

    SystemCKernelSessionLimits tiny = limits;
    tiny.max_name_bytes = 3U;
    diagnostics = { };
    assert(!fsim::systemc::serialize_systemc_bind_endpoint_payload(
        binding, tiny, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-S002");
}

void run_successful_lifecycle(fsim::systemc::SystemCKernelBackend& backend,
    const std::filesystem::path& plugin, const std::string& identity,
    const std::string& hierarchy_path, const std::string& root,
    const std::int64_t width, std::uint64_t& sequence,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits)
{
    const auto island = island_id(identity, protocol_limits);
    const auto ids = object_ids(island, hierarchy_path, root,
        root + ".leaf.input", protocol_limits);
    auto created = transact(backend, SystemCKernelOperation::create_session,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, island, { }, { }, { },
        session_payload(identity, plugin, session_limits));
    assert(created.receipt.state == SystemCKernelSessionState::constructing);
    assert(!created.receipt.published);
    assert(created.receipt.context_generation != 0U);

    auto object = transact(backend, SystemCKernelOperation::create_object,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, island, ids.hierarchy, ids.object, { },
        object_payload(hierarchy_path, root, width, false, session_limits));
    assert(object.receipt.staged_objects == 1U);
    assert(object.receipt.published_objects == 0U);

    auto binding = transact(backend, SystemCKernelOperation::bind_endpoint,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, island, ids.hierarchy, ids.object, ids.endpoint,
        binding_payload(root, root + ".channel", session_limits));
    assert(binding.receipt.staged_bindings == 1U);

    auto elaborated = transact(backend, SystemCKernelOperation::elaborate,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, island);
    assert(elaborated.receipt.state == SystemCKernelSessionState::elaborated);
    assert(!elaborated.receipt.published);

    auto started = transact(backend, SystemCKernelOperation::start,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, island);
    assert(started.receipt.state == SystemCKernelSessionState::quiescent);
    assert(started.receipt.published);
    assert(started.receipt.published_objects == 1U);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path plugin = argv[1];
    std::string error;
    auto probe = fsim::platform::DynamicLibrary::open(plugin, error);
    assert(probe && error.empty());
    using Reset = void (*)() noexcept;
    using Counter = std::size_t (*)(std::uint32_t) noexcept;
    const auto reset = reinterpret_cast<Reset>(
        probe->symbol("fsim_test_session_reset_v1", error));
    assert(reset != nullptr && error.empty());
    const auto counter = reinterpret_cast<Counter>(
        probe->symbol("fsim_test_session_counter_v1", error));
    assert(counter != nullptr && error.empty());

    const SystemCKernelProtocolLimits protocol_limits;
    const SystemCKernelSessionLimits session_limits;
    audit_payload_codecs(session_limits);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);

    fsim::diagnostic::Engine diagnostics;
    auto backend = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(backend && diagnostics.diagnostics().empty());
    std::uint64_t sequence = 1U;
    auto handshake = transact(*backend, SystemCKernelOperation::handshake,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits);
    assert(handshake.receipt.state == SystemCKernelSessionState::vacant);

    reset();
    run_successful_lifecycle(*backend, plugin, "primary-session",
        "primary-roots", "top", 13, sequence, protocol_limits,
        session_limits);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 1U);
    assert(counter(0U) == 1U);
    assert(counter(1U) == 1U);
    assert(counter(3U) == 1U);
    assert(counter(4U) == 1U);
    assert(counter(5U) == 1U);
    assert(counter(6U) == 1U);
    assert(counter(8U) == 13U);

    const auto primary_island = island_id("primary-session", protocol_limits);
    auto repeated_start = transact(*backend, SystemCKernelOperation::start,
        SystemCKernelMessageStatus::rejected, { sequence++ }, protocol_limits,
        session_limits, primary_island);
    assert(repeated_start.receipt.state
        == SystemCKernelSessionState::quiescent);
    auto terminal = transact(*backend, SystemCKernelOperation::teardown,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, primary_island);
    assert(terminal.receipt.state == SystemCKernelSessionState::terminal);
    assert(!terminal.receipt.published);
    assert(terminal.receipt.staged_objects == 0U);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);
    assert(counter(0U) == 0U);
    assert(counter(2U) == 1U);
    assert(counter(7U) == 1U);
    const auto ended = counter(7U);
    const auto destroyed = counter(2U);
    auto repeated_teardown = transact(*backend,
        SystemCKernelOperation::teardown, SystemCKernelMessageStatus::ok,
        { sequence++ }, protocol_limits, session_limits, primary_island);
    assert(repeated_teardown.receipt.state
        == SystemCKernelSessionState::terminal);
    assert(counter(7U) == ended && counter(2U) == destroyed);

    reset();
    diagnostics = { };
    auto failing = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(failing);
    const auto failing_island = island_id("failing-session", protocol_limits);
    const auto failing_ids = object_ids(failing_island, "failure-roots",
        "failing_top", "failing_top.leaf.input", protocol_limits);
    transact(*failing, SystemCKernelOperation::create_session,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, failing_island, { }, { }, { },
        session_payload("failing-session", plugin, session_limits));
    auto failed_object = transact(*failing,
        SystemCKernelOperation::create_object,
        SystemCKernelMessageStatus::failed, { sequence++ }, protocol_limits,
        session_limits, failing_island, failing_ids.hierarchy,
        failing_ids.object, { }, object_payload("failure-roots", "failing_top", 9, true, session_limits));
    assert(failed_object.receipt.state == SystemCKernelSessionState::failed);
    assert(failed_object.receipt.code
        == SystemCKernelLifecycleCode::upstream);
    assert(failed_object.receipt.staged_objects == 0U);
    assert(!failed_object.receipt.published);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);
    assert(counter(0U) == 0U);
    assert(counter(1U) == 0U);

    reset();
    diagnostics = { };
    auto repeated = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(repeated);
    const auto repeated_island = island_id("repeated-session", protocol_limits);
    const auto repeated_ids = object_ids(repeated_island, "repeat-roots",
        "repeat_top", "repeat_top.leaf.input", protocol_limits);
    transact(*repeated, SystemCKernelOperation::create_session,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, repeated_island, { }, { }, { },
        session_payload("repeated-session", plugin, session_limits));
    const auto repeated_payload = object_payload("repeat-roots",
        "repeat_top", 5, false, session_limits);
    transact(*repeated, SystemCKernelOperation::create_object,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, repeated_island, repeated_ids.hierarchy,
        repeated_ids.object, { }, repeated_payload);
    auto duplicate = transact(*repeated,
        SystemCKernelOperation::create_object,
        SystemCKernelMessageStatus::failed, { sequence++ }, protocol_limits,
        session_limits, repeated_island, repeated_ids.hierarchy,
        repeated_ids.object, { }, repeated_payload);
    assert(duplicate.receipt.state == SystemCKernelSessionState::failed);
    assert(duplicate.receipt.code
        == SystemCKernelLifecycleCode::session_state);
    assert(duplicate.receipt.staged_objects == 0U);
    assert(counter(0U) == 0U && counter(2U) == 1U);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);

    reset();
    diagnostics = { };
    auto bad_binding = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(bad_binding);
    const auto bad_binding_island = island_id("bad-binding-session", protocol_limits);
    const auto bad_binding_ids = object_ids(bad_binding_island,
        "bad-binding-roots", "bad_binding_top",
        "bad_binding_top.leaf.input", protocol_limits);
    transact(*bad_binding, SystemCKernelOperation::create_session,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, bad_binding_island, { }, { }, { }, session_payload("bad-binding-session", plugin, session_limits));
    transact(*bad_binding, SystemCKernelOperation::create_object,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, bad_binding_island, bad_binding_ids.hierarchy,
        bad_binding_ids.object, { }, object_payload("bad-binding-roots", "bad_binding_top", 11, false, session_limits));
    auto rejected_binding = transact(*bad_binding,
        SystemCKernelOperation::bind_endpoint,
        SystemCKernelMessageStatus::failed, { sequence++ }, protocol_limits,
        session_limits, bad_binding_island, bad_binding_ids.hierarchy,
        bad_binding_ids.object, bad_binding_ids.endpoint,
        binding_payload("bad_binding_top", "bad_binding_top.missing",
            session_limits));
    assert(rejected_binding.receipt.state
        == SystemCKernelSessionState::failed);
    assert(rejected_binding.receipt.staged_objects == 0U);
    assert(counter(0U) == 0U && counter(2U) == 1U);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);

    reset();
    diagnostics = { };
    auto unbound = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(unbound);
    const auto unbound_island = island_id("unbound-session", protocol_limits);
    const auto unbound_ids = object_ids(unbound_island, "unbound-roots",
        "unbound_top", "unbound_top.leaf.input", protocol_limits);
    transact(*unbound, SystemCKernelOperation::create_session,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, unbound_island, { }, { }, { },
        session_payload("unbound-session", plugin, session_limits));
    transact(*unbound, SystemCKernelOperation::create_object,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, unbound_island, unbound_ids.hierarchy,
        unbound_ids.object, { }, object_payload("unbound-roots", "unbound_top", 7, false, session_limits));
    auto failed_elaboration = transact(*unbound,
        SystemCKernelOperation::elaborate,
        SystemCKernelMessageStatus::failed, { sequence++ }, protocol_limits,
        session_limits, unbound_island);
    assert(failed_elaboration.receipt.state
        == SystemCKernelSessionState::failed);
    assert(failed_elaboration.receipt.staged_objects == 0U);
    assert(counter(0U) == 0U && counter(2U) == 1U);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);

    reset();
    diagnostics = { };
    auto repeated_session = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(repeated_session);
    const auto repeated_session_island = island_id("repeat-create-session", protocol_limits);
    const auto repeated_session_bytes = session_payload(
        "repeat-create-session", plugin, session_limits);
    transact(*repeated_session, SystemCKernelOperation::create_session,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, repeated_session_island, { }, { }, { },
        repeated_session_bytes);
    auto rejected_session = transact(*repeated_session,
        SystemCKernelOperation::create_session,
        SystemCKernelMessageStatus::rejected, { sequence++ }, protocol_limits,
        session_limits, repeated_session_island, { }, { }, { },
        repeated_session_bytes);
    assert(rejected_session.receipt.state
        == SystemCKernelSessionState::failed);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);

    reset();
    diagnostics = { };
    auto left = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    auto right = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(left && right);
    run_successful_lifecycle(*left, plugin, "isolated-left", "left-roots",
        "shared_name", 21, sequence, protocol_limits, session_limits);
    run_successful_lifecycle(*right, plugin, "isolated-right", "right-roots",
        "shared_name", 34, sequence, protocol_limits, session_limits);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 2U);
    const auto left_island = island_id("isolated-left", protocol_limits);
    const auto right_island = island_id("isolated-right", protocol_limits);
    transact(*left, SystemCKernelOperation::teardown,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, left_island);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 1U);
    transact(*right, SystemCKernelOperation::teardown,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, right_island);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);
    assert(counter(0U) == 0U);
    assert(counter(1U) == 2U && counter(2U) == 2U);
    assert(counter(3U) == 2U && counter(4U) == 2U);
    assert(counter(5U) == 2U && counter(6U) == 2U);
    assert(counter(7U) == 2U);

    diagnostics = { };
    SystemCKernelSessionLimits invalid_limits = session_limits;
    invalid_limits.max_detail_bytes = protocol_limits.max_payload_bytes + 1U;
    assert(!fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, invalid_limits, diagnostics));
    assert(diagnostics.diagnostics().back().code == "FSIM-SC-S003");

    diagnostics = { };
    auto missing = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(missing);
    const auto missing_island = island_id("missing-plugin", protocol_limits);
    auto missing_response = transact(*missing,
        SystemCKernelOperation::create_session,
        SystemCKernelMessageStatus::failed, { sequence++ }, protocol_limits,
        session_limits, missing_island, { }, { }, { }, session_payload("missing-plugin", plugin.string() + ".missing", session_limits));
    assert(missing_response.receipt.state
        == SystemCKernelSessionState::vacant);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);
    return 0;
}
