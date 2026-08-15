// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_execution.hpp"
#include "fsim/systemc/kernel_backend_loopback.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fsim::systemc;

struct Identities {
    SystemCIslandId island;
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCEndpointId input;
    SystemCEndpointId output;
};

struct LifecycleResponse {
    SystemCKernelMessage message;
    SystemCKernelLifecycleReceipt receipt;
};

struct ExecutionResponse {
    SystemCKernelMessage message;
    SystemCKernelExecutionReceipt receipt;
};

Identities identities(const std::string& identity, const std::string& root,
    const SystemCKernelProtocolLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const auto island = make_systemc_island_id(identity, limits, diagnostics);
    const auto hierarchy = island
        ? make_systemc_hierarchy_id(*island, "execution-roots", limits,
              diagnostics)
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

SystemCKernelMessage response_message(SystemCKernelBackend& backend,
    const SystemCKernelOperation operation,
    const SystemCKernelMessageStatus expected_status,
    const SystemCSequenceId sequence,
    const SystemCKernelProtocolLimits& limits, const Identities& ids,
    const SystemCEndpointId endpoint = { },
    std::vector<std::byte> payload = { })
{
    SystemCKernelMessage request;
    request.header.operation = operation;
    request.header.direction = SystemCKernelMessageDirection::request;
    request.header.sequence = sequence;
    request.header.island = ids.island;
    if (endpoint.valid()) {
        request.header.hierarchy = ids.hierarchy;
        request.header.object = ids.object;
        request.header.endpoint = endpoint;
    } else if (operation == SystemCKernelOperation::create_object) {
        request.header.hierarchy = ids.hierarchy;
        request.header.object = ids.object;
    }
    request.payload = std::move(payload);

    fsim::diagnostic::Engine diagnostics;
    const auto encoded = serialize_systemc_kernel_message(
        request, limits, diagnostics);
    assert(encoded && diagnostics.diagnostics().empty());
    const auto transport = backend.exchange(*encoded);
    assert(transport.status == SystemCKernelTransportStatus::ok);
    diagnostics = { };
    auto response = deserialize_systemc_kernel_message(
        transport.bytes, limits, diagnostics);
    assert(response && diagnostics.diagnostics().empty());
    assert(response->header.operation == operation);
    assert(response->header.direction
        == SystemCKernelMessageDirection::response);
    assert(response->header.status == expected_status);
    assert(response->header.correlation == sequence);
    return std::move(*response);
}

LifecycleResponse lifecycle(SystemCKernelBackend& backend,
    const SystemCKernelOperation operation,
    const SystemCKernelMessageStatus expected_status,
    const SystemCSequenceId sequence,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits, const Identities& ids,
    const SystemCEndpointId endpoint = { },
    std::vector<std::byte> payload = { })
{
    auto message = response_message(backend, operation, expected_status,
        sequence, protocol_limits, ids, endpoint, std::move(payload));
    fsim::diagnostic::Engine diagnostics;
    auto receipt = deserialize_systemc_lifecycle_receipt(
        message.payload, session_limits, diagnostics);
    assert(receipt && diagnostics.diagnostics().empty());
    return { std::move(message), std::move(*receipt) };
}

ExecutionResponse execute(SystemCKernelBackend& backend,
    const SystemCKernelOperation operation,
    const SystemCKernelMessageStatus expected_status,
    const SystemCSequenceId sequence,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    const Identities& ids, const SystemCEndpointId endpoint = { },
    std::vector<std::byte> payload = { })
{
    auto message = response_message(backend, operation, expected_status,
        sequence, protocol_limits, ids, endpoint, std::move(payload));
    fsim::diagnostic::Engine diagnostics;
    auto receipt = deserialize_systemc_execution_receipt(
        message.payload, execution_limits, diagnostics);
    assert(receipt && diagnostics.diagnostics().empty());
    if (expected_status == SystemCKernelMessageStatus::ok) {
        assert(receipt->code == SystemCKernelExecutionCode::none);
    } else {
        assert(receipt->code != SystemCKernelExecutionCode::none);
        assert(*systemc_kernel_execution_diagnostic_code(receipt->code) != '\0');
    }
    return { std::move(message), std::move(*receipt) };
}

std::vector<std::byte> session_payload(const std::string& identity,
    const std::filesystem::path& plugin,
    const SystemCKernelSessionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_create_session_payload(
        { identity, plugin.string(), 1U }, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

std::vector<std::byte> object_payload(const std::string& root,
    const SystemCKernelSessionLimits& limits,
    const std::string& factory = "execution_root")
{
    fsim::diagnostic::Engine diagnostics;
    SystemCKernelCreateObjectPayload payload;
    payload.hierarchy_path = "execution-roots";
    payload.object_path = root;
    payload.factory = factory;
    payload.instance = root;
    auto result = serialize_systemc_create_object_payload(
        payload, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

std::vector<std::byte> binding_payload(const std::string& endpoint,
    const std::string& channel, const SystemCKernelSessionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_bind_endpoint_payload(
        { endpoint, channel }, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

std::vector<std::byte> input_payload(const std::uint64_t value,
    const SystemCKernelExecutionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_apply_inputs_payload(
        { { value, 32U, false } }, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

std::vector<std::byte> input_payload(const SystemCKernelValue& value,
    const SystemCKernelExecutionLimits& limits)
{
    SystemCKernelScalarValue crossing;
    crossing.typed = value;
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_apply_inputs_payload(
        { crossing }, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

std::vector<std::byte> advance_payload(const SystemCKernelAdvanceKind kind,
    const std::uint64_t duration_fs,
    const SystemCKernelExecutionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto result = serialize_systemc_advance_payload(
        { kind, duration_fs }, limits, diagnostics);
    assert(result && diagnostics.diagnostics().empty());
    return std::move(*result);
}

void construct_and_start(SystemCKernelBackend& backend,
    const std::filesystem::path& plugin, const std::string& identity,
    const std::string& root, std::uint64_t& sequence,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const std::string& factory = "execution_root")
{
    const auto ids = identities(identity, root, protocol_limits);
    auto created = lifecycle(backend, SystemCKernelOperation::create_session,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids, { },
        session_payload(identity, plugin, session_limits));
    assert(created.receipt.state == SystemCKernelSessionState::constructing);
    lifecycle(backend, SystemCKernelOperation::create_object,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids, { }, object_payload(root, session_limits, factory));
    lifecycle(backend, SystemCKernelOperation::bind_endpoint,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids, ids.input,
        binding_payload(root + ".input", root + ".input_channel",
            session_limits));
    lifecycle(backend, SystemCKernelOperation::bind_endpoint,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids, ids.output,
        binding_payload(root + ".output", root + ".output_channel",
            session_limits));
    lifecycle(backend, SystemCKernelOperation::elaborate,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids);
    auto started = lifecycle(backend, SystemCKernelOperation::start,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids);
    assert(started.receipt.state == SystemCKernelSessionState::quiescent);
    assert(started.receipt.published);
}

void audit_codecs(const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelExecutionLimits& limits)
{
    fsim::diagnostic::Engine diagnostics;
    const SystemCKernelApplyInputsPayload input { { 0xabcU, 12U, false } };
    auto input_bytes = serialize_systemc_apply_inputs_payload(
        input, limits, diagnostics);
    assert(input_bytes && diagnostics.diagnostics().empty());
    assert(deserialize_systemc_apply_inputs_payload(
               *input_bytes, limits, diagnostics)
        == input);
    input_bytes->push_back(std::byte { 0U });
    diagnostics = { };
    assert(!deserialize_systemc_apply_inputs_payload(
        *input_bytes, limits, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-E002");

    diagnostics = { };
    assert(!serialize_systemc_apply_inputs_payload(
        { { 4U, 2U, false } }, limits, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-E002");

    const SystemCKernelAdvancePayload advance {
        SystemCKernelAdvanceKind::time, 5'000'000U
    };
    diagnostics = { };
    auto advance_bytes = serialize_systemc_advance_payload(
        advance, limits, diagnostics);
    assert(advance_bytes);
    assert(deserialize_systemc_advance_payload(
               *advance_bytes, limits, diagnostics)
        == advance);
    diagnostics = { };
    assert(!serialize_systemc_advance_payload(
        { SystemCKernelAdvanceKind::delta, 1U }, limits, diagnostics));

    const auto ids = identities("codec-execution", "codec_top",
        protocol_limits);
    SystemCKernelExecutionReceipt receipt;
    receipt.session_state = SystemCKernelSessionState::quiescent;
    receipt.status = SystemCKernelExecutionStatus::quiescent;
    receipt.published = true;
    receipt.future_activity = true;
    receipt.next_activity_time_fs = 10U;
    receipt.order = { 5U, 2U, SystemCAccelleraRegion::quiescent,
        ids.island, { 3U } };
    receipt.samples = {
        { ids.input,
            { 5U, 2U, SystemCAccelleraRegion::evaluate, ids.island, { 1U } },
            { 7U, 32U, false }, false },
        { ids.output,
            { 5U, 2U, SystemCAccelleraRegion::update, ids.island, { 2U } },
            { 22U, 32U, false }, true },
    };
    receipt.detail = "bounded codec receipt";
    diagnostics = { };
    auto receipt_bytes = serialize_systemc_execution_receipt(
        receipt, limits, diagnostics);
    assert(receipt_bytes && diagnostics.diagnostics().empty());
    assert(deserialize_systemc_execution_receipt(
               *receipt_bytes, limits, diagnostics)
        == receipt);
    receipt_bytes->pop_back();
    diagnostics = { };
    assert(!deserialize_systemc_execution_receipt(
        *receipt_bytes, limits, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-E002");

    std::swap(receipt.samples[0], receipt.samples[1]);
    diagnostics = { };
    assert(!serialize_systemc_execution_receipt(
        receipt, limits, diagnostics));
    diagnostics = { };
    SystemCKernelExecutionLimits invalid = limits;
    invalid.max_samples_per_message = 0U;
    assert(!serialize_systemc_apply_inputs_payload(input, invalid, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-E003");
}

SystemCKernelValue wide_logic4_value()
{
    SystemCKernelValue value;
    value.kind = SystemCKernelValueKind::logic4;
    value.width = 257U;
    value.range = { 256, 0, SystemCKernelRangeDirection::descending };
    value.planes.assign(2U, std::vector<std::uint64_t>(5U));
    for (std::size_t word = 0U; word < 5U; ++word) {
        value.planes[0][word] = 0x5555555555555555ULL
            ^ static_cast<std::uint64_t>(word);
        value.planes[1][word] = 0x3333333333333333ULL
            ^ (static_cast<std::uint64_t>(word) << 4U);
    }
    value.planes[0].back() &= 1U;
    value.planes[1].back() &= 1U;
    return value;
}

SystemCKernelValue wide_bit2_value()
{
    SystemCKernelValue value;
    value.kind = SystemCKernelValueKind::bit2;
    value.width = 129U;
    value.range = { 128, 0, SystemCKernelRangeDirection::descending };
    value.planes = { { 0x0123456789abcdefULL,
        0xfedcba9876543210ULL, 1U } };
    return value;
}

void run_typed_loopback_execution(const std::filesystem::path& plugin,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    const SystemCKernelValue& expected, const std::string& factory,
    const std::string& identity, const std::string& root)
{
    fsim::diagnostic::Engine diagnostics;
    auto backend = make_systemc_kernel_loopback_session_backend(protocol_limits,
        session_limits, execution_limits, { }, diagnostics);
    assert(backend && diagnostics.diagnostics().empty());
    std::uint64_t sequence = 3000U;
    const auto ids = identities(identity, root, protocol_limits);
    construct_and_start(*backend, plugin, identity, root, sequence,
        protocol_limits, session_limits, factory);

    execute(*backend, SystemCKernelOperation::drain_outputs,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output);
    auto applied = execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input,
        input_payload(expected, execution_limits));
    assert(applied.receipt.current_activity);
    auto advanced = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    assert(advanced.receipt.status == SystemCKernelExecutionStatus::quiescent);

    auto output = execute(*backend, SystemCKernelOperation::drain_outputs,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output);
    assert(output.receipt.samples.size() == 1U);
    assert(output.receipt.samples.front().value.typed == expected);
    auto input = execute(*backend, SystemCKernelOperation::inspect,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input);
    assert(input.receipt.samples.size() == 1U);
    assert(input.receipt.samples.front().value.typed == expected);
    auto snapshot = execute(*backend, SystemCKernelOperation::snapshot,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids);
    assert(snapshot.receipt.samples.size() == 2U);
    assert(std::ranges::all_of(snapshot.receipt.samples,
        [&](const auto& sample) { return sample.value.typed == expected; }));

    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelMessageStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(7U, execution_limits));
    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelMessageStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output,
        input_payload(expected, execution_limits));
    lifecycle(*backend, SystemCKernelOperation::teardown,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids);
}

void run_execution(const std::filesystem::path& plugin,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto backend = make_systemc_kernel_session_backend(protocol_limits,
        session_limits, execution_limits, diagnostics);
    assert(backend && diagnostics.diagnostics().empty());
    std::uint64_t sequence = 1U;
    const std::string identity = "execution-session";
    const std::string root = "execution_top";
    const auto ids = identities(identity, root, protocol_limits);
    construct_and_start(*backend, plugin, identity, root, sequence,
        protocol_limits, session_limits);

    auto initial_activity = execute(*backend,
        SystemCKernelOperation::next_activity, SystemCKernelMessageStatus::ok,
        { sequence++ }, protocol_limits, execution_limits, ids);
    assert(!initial_activity.receipt.current_activity);
    assert(initial_activity.receipt.future_activity);
    assert(initial_activity.receipt.next_activity_time_fs == 5'000'000U);

    auto initial_output = execute(*backend,
        SystemCKernelOperation::drain_outputs, SystemCKernelMessageStatus::ok,
        { sequence++ }, protocol_limits, execution_limits, ids, ids.output);
    assert(initial_output.receipt.samples.size() == 1U);
    assert(initial_output.receipt.samples.front().dirty);
    assert(initial_output.receipt.samples.front().value.bits == 1U);
    auto clean_output = execute(*backend,
        SystemCKernelOperation::drain_outputs, SystemCKernelMessageStatus::ok,
        { sequence++ }, protocol_limits, execution_limits, ids, ids.output);
    assert(clean_output.receipt.samples.empty());

    auto applied = execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(7U, execution_limits));
    assert(applied.receipt.current_activity);
    assert(applied.receipt.next_activity_time_fs == 0U);
    auto advanced = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    assert(advanced.receipt.order.time_fs == 0U);
    assert(!advanced.receipt.current_activity);
    assert(advanced.receipt.status == SystemCKernelExecutionStatus::quiescent);

    auto output = execute(*backend, SystemCKernelOperation::drain_outputs,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output);
    assert(output.receipt.samples.size() == 1U);
    assert(output.receipt.samples.front().value.bits == 22U);
    auto input = execute(*backend, SystemCKernelOperation::inspect,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input);
    assert(input.receipt.samples.size() == 1U);
    assert(input.receipt.samples.front().value.bits == 7U);
    auto snapshot = execute(*backend, SystemCKernelOperation::snapshot,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids);
    assert(snapshot.receipt.samples.size() == 2U);
    assert(std::ranges::is_sorted(snapshot.receipt.samples, { },
        &SystemCKernelExecutionSample::order));
    auto report = execute(*backend, SystemCKernelOperation::report,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids);
    assert(report.receipt.detail.find("identity=execution-session")
        != std::string::npos);

    auto timed = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::time, 5'000'000U,
            execution_limits));
    assert(timed.receipt.order.time_fs == 5'000'000U);
    assert(timed.receipt.current_activity);
    assert(timed.receipt.next_activity_time_fs == 5'000'000U);
    auto timed_delta = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    assert(!timed_delta.receipt.current_activity);
    assert(timed_delta.receipt.future_activity);
    assert(timed_delta.receipt.next_activity_time_fs == 10'000'000U);

    auto wrong_direction = execute(*backend,
        SystemCKernelOperation::apply_inputs,
        SystemCKernelMessageStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output, input_payload(9U, execution_limits));
    assert(wrong_direction.receipt.session_state
        == SystemCKernelSessionState::quiescent);
    auto malformed = input_payload(9U, execution_limits);
    malformed.pop_back();
    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelMessageStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, std::move(malformed));

    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(99U, execution_limits));
    auto paused = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::time, 1U,
            execution_limits));
    assert(paused.receipt.status == SystemCKernelExecutionStatus::paused);
    auto resumed = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::time, 1U,
            execution_limits));
    assert(resumed.receipt.status == SystemCKernelExecutionStatus::quiescent);

    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(100U, execution_limits));
    auto stopped = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    assert(stopped.receipt.status == SystemCKernelExecutionStatus::stopped);
    execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelMessageStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    auto terminal = lifecycle(*backend, SystemCKernelOperation::teardown,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids);
    assert(terminal.receipt.state == SystemCKernelSessionState::terminal);
}

void run_error_rollback(const std::filesystem::path& plugin,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto backend = make_systemc_kernel_session_backend(protocol_limits,
        session_limits, execution_limits, diagnostics);
    assert(backend);
    std::uint64_t sequence = 1000U;
    const std::string identity = "execution-error-session";
    const std::string root = "execution_error_top";
    const auto ids = identities(identity, root, protocol_limits);
    construct_and_start(*backend, plugin, identity, root, sequence,
        protocol_limits, session_limits);
    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(101U, execution_limits));
    auto failed = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelMessageStatus::failed, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    assert(failed.receipt.status == SystemCKernelExecutionStatus::error);
    assert(failed.receipt.session_state == SystemCKernelSessionState::failed);
    assert(!failed.receipt.published);
    assert(systemc_kernel_backend_live_contexts() == 0U);
}

void run_snapshot_limit(const std::filesystem::path& plugin,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits)
{
    SystemCKernelExecutionLimits limits;
    limits.max_samples_per_message = 1U;
    fsim::diagnostic::Engine diagnostics;
    auto backend = make_systemc_kernel_session_backend(
        protocol_limits, session_limits, limits, diagnostics);
    assert(backend);
    std::uint64_t sequence = 2000U;
    const std::string identity = "execution-limit-session";
    const std::string root = "execution_limit_top";
    const auto ids = identities(identity, root, protocol_limits);
    construct_and_start(*backend, plugin, identity, root, sequence,
        protocol_limits, session_limits);
    auto rejected = execute(*backend, SystemCKernelOperation::snapshot,
        SystemCKernelMessageStatus::rejected, { sequence++ }, protocol_limits,
        limits, ids);
    assert(rejected.receipt.code == SystemCKernelExecutionCode::resource);
    lifecycle(*backend, SystemCKernelOperation::teardown,
        SystemCKernelMessageStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::filesystem::path plugin = argv[1];
    const std::filesystem::path value_plugin = argv[2];
    const SystemCKernelProtocolLimits protocol_limits;
    const SystemCKernelSessionLimits session_limits;
    const SystemCKernelExecutionLimits execution_limits;
    audit_codecs(protocol_limits, execution_limits);
    assert(systemc_kernel_backend_live_contexts() == 0U);
    run_execution(plugin, protocol_limits, session_limits, execution_limits);
    assert(systemc_kernel_backend_live_contexts() == 0U);
    run_error_rollback(plugin, protocol_limits, session_limits,
        execution_limits);
    run_snapshot_limit(plugin, protocol_limits, session_limits);
    assert(systemc_kernel_backend_live_contexts() == 0U);
    run_typed_loopback_execution(value_plugin, protocol_limits, session_limits,
        execution_limits, wide_logic4_value(), "value_root",
        "typed-loopback-session", "typed_loopback_top");
    assert(systemc_kernel_backend_live_contexts() == 0U);
    run_typed_loopback_execution(value_plugin, protocol_limits, session_limits,
        execution_limits, wide_bit2_value(), "bit_value_root",
        "bit-loopback-session", "bit_loopback_top");
    assert(systemc_kernel_backend_live_contexts() == 0U);

    fsim::diagnostic::Engine diagnostics;
    SystemCKernelExecutionLimits invalid;
    invalid.max_detail_bytes = 0U;
    assert(!make_systemc_kernel_session_backend(protocol_limits,
        session_limits, invalid, diagnostics));
    assert(diagnostics.diagnostics().front().code == "FSIM-SC-E003");
    return 0;
}
