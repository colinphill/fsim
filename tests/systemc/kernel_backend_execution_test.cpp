// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_direct.hpp"
#include "fsim/systemc/kernel_backend_execution.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace fsim::systemc;

// Test-only selector used to exercise each typed request through one helper.
enum class SystemCKernelOperation {
    create_session,
    create_object,
    bind_endpoint,
    elaborate,
    start,
    apply_inputs,
    advance,
    next_activity,
    drain_outputs,
    report,
    inspect,
    snapshot,
    teardown,
};

struct Identities {
    SystemCIslandId island;
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCEndpointId input;
    SystemCEndpointId output;
};

struct LifecycleResponse {
    SystemCKernelDirectResultStatus status;
    SystemCKernelLifecycleReceipt receipt;
};

struct ExecutionResponse {
    SystemCKernelDirectResultStatus status;
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

using ExecutionTestPayload = std::variant<std::monostate,
    SystemCKernelCreateSessionPayload, SystemCKernelCreateObjectPayload,
    SystemCKernelBindEndpointPayload, SystemCKernelApplyInputsPayload,
    SystemCKernelAdvancePayload>;

SystemCKernelDirectResult request_backend(SystemCKernelBackend& backend,
    const SystemCKernelOperation operation,
    const SystemCSequenceId sequence,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    const Identities& ids,
    const SystemCEndpointId endpoint = { },
    ExecutionTestPayload payload = { })
{
    static_cast<void>(session_limits);
    static_cast<void>(execution_limits);
    SystemCKernelDirectRequest request;
    switch (operation) {
    case SystemCKernelOperation::create_session: {
        auto direct_payload = std::get<SystemCKernelCreateSessionPayload>(
            std::move(payload));
        request = SystemCKernelCreateSessionRequest {
            ids.island, sequence, std::move(direct_payload)
        };
        break;
    }
    case SystemCKernelOperation::create_object: {
        auto direct_payload = std::get<SystemCKernelCreateObjectPayload>(
            std::move(payload));
        request = SystemCKernelCreateObjectRequest { ids.island, sequence,
            ids.hierarchy, ids.object, std::move(direct_payload) };
        break;
    }
    case SystemCKernelOperation::bind_endpoint: {
        auto direct_payload = std::get<SystemCKernelBindEndpointPayload>(
            std::move(payload));
        request = SystemCKernelBindEndpointRequest { ids.island, sequence,
            ids.hierarchy, ids.object, endpoint, std::move(direct_payload) };
        break;
    }
    case SystemCKernelOperation::elaborate:
        request = SystemCKernelElaborateRequest { ids.island, sequence };
        break;
    case SystemCKernelOperation::start:
        request = SystemCKernelStartRequest { ids.island, sequence };
        break;
    case SystemCKernelOperation::apply_inputs: {
        auto direct_payload = std::get<SystemCKernelApplyInputsPayload>(
            std::move(payload));
        request = SystemCKernelApplyInputsRequest { ids.island, sequence,
            ids.object, endpoint, std::move(direct_payload) };
        break;
    }
    case SystemCKernelOperation::advance: {
        auto direct_payload = std::get<SystemCKernelAdvancePayload>(
            std::move(payload));
        request = SystemCKernelAdvanceRequest {
            ids.island, sequence, std::move(direct_payload)
        };
        break;
    }
    case SystemCKernelOperation::next_activity:
        request = SystemCKernelNextActivityRequest { ids.island, sequence };
        break;
    case SystemCKernelOperation::drain_outputs:
        request = SystemCKernelDrainOutputsRequest { ids.island, sequence,
            ids.object, endpoint };
        break;
    case SystemCKernelOperation::report:
        request = SystemCKernelReportRequest { ids.island, sequence };
        break;
    case SystemCKernelOperation::inspect:
        request = SystemCKernelInspectRequest { ids.island, sequence,
            ids.object, endpoint };
        break;
    case SystemCKernelOperation::snapshot:
        request = SystemCKernelSnapshotRequest { ids.island, sequence };
        break;
    case SystemCKernelOperation::teardown:
        request = SystemCKernelTeardownRequest { ids.island, sequence };
        break;
    default:
        assert(false && "execution test used a non-direct kernel operation");
        return { SystemCKernelDirectResultStatus::disconnected, { } };
    }
    return backend.request(request);
}

LifecycleResponse lifecycle(SystemCKernelBackend& backend,
    const SystemCKernelOperation operation,
    const SystemCKernelDirectResultStatus expected_status,
    const SystemCSequenceId sequence,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits, const Identities& ids,
    const SystemCEndpointId endpoint = { },
    ExecutionTestPayload payload = { })
{
    static_cast<void>(protocol_limits);
    auto result = request_backend(backend, operation, sequence, session_limits,
        SystemCKernelExecutionLimits { }, ids, endpoint, std::move(payload));
    assert(result.status == expected_status);
    auto receipt = std::get<SystemCKernelLifecycleReceipt>(
        std::move(result.receipt));
    return { result.status, std::move(receipt) };
}

ExecutionResponse execute(SystemCKernelBackend& backend,
    const SystemCKernelOperation operation,
    const SystemCKernelDirectResultStatus expected_status,
    const SystemCSequenceId sequence,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    const Identities& ids, const SystemCEndpointId endpoint = { },
    ExecutionTestPayload payload = { })
{
    static_cast<void>(protocol_limits);
    auto result = request_backend(backend, operation, sequence,
        SystemCKernelSessionLimits { },
        execution_limits, ids, endpoint, std::move(payload));
    assert(result.status == expected_status);
    auto receipt = std::get<SystemCKernelExecutionReceipt>(
        std::move(result.receipt));
    if (expected_status == SystemCKernelDirectResultStatus::ok) {
        assert(receipt.code == SystemCKernelExecutionCode::none);
    } else {
        assert(receipt.code != SystemCKernelExecutionCode::none);
        assert(*systemc_kernel_execution_diagnostic_code(receipt.code) != '\0');
    }
    return { result.status, std::move(receipt) };
}

SystemCKernelCreateSessionPayload session_payload(const std::string& identity,
    const std::filesystem::path& plugin,
    const SystemCKernelSessionLimits& limits)
{
    const auto plugin_path = plugin.string();
    assert(identity.size() <= limits.max_identity_bytes);
    assert(plugin_path.size() <= limits.max_plugin_path_bytes);
    return { identity, plugin_path, 1U };
}

SystemCKernelCreateObjectPayload object_payload(const std::string& root,
    const SystemCKernelSessionLimits& limits,
    const std::string& factory = "execution_root")
{
    SystemCKernelCreateObjectPayload payload;
    payload.hierarchy_path = "execution-roots";
    payload.object_path = root;
    payload.factory = factory;
    payload.instance = root;
    assert(root.size() <= limits.max_name_bytes);
    return payload;
}

SystemCKernelBindEndpointPayload binding_payload(const std::string& endpoint,
    const std::string& channel, const SystemCKernelSessionLimits& limits)
{
    assert(endpoint.size() <= limits.max_name_bytes);
    assert(channel.size() <= limits.max_name_bytes);
    return { endpoint, channel };
}

SystemCKernelApplyInputsPayload input_payload(const std::uint64_t value,
    const SystemCKernelExecutionLimits& limits)
{
    static_cast<void>(limits);
    return { { value, 32U, false } };
}

SystemCKernelApplyInputsPayload input_payload(const SystemCKernelValue& value,
    const SystemCKernelExecutionLimits& limits)
{
    SystemCKernelScalarValue crossing;
    crossing.typed = value;
    static_cast<void>(limits);
    return { crossing };
}

SystemCKernelAdvancePayload advance_payload(const SystemCKernelAdvanceKind kind,
    const std::uint64_t duration_fs,
    const SystemCKernelExecutionLimits& limits)
{
    static_cast<void>(limits);
    return { kind, duration_fs };
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
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids, { },
        session_payload(identity, plugin, session_limits));
    assert(created.receipt.state == SystemCKernelSessionState::constructing);
    lifecycle(backend, SystemCKernelOperation::create_object,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids, { }, object_payload(root, session_limits, factory));
    lifecycle(backend, SystemCKernelOperation::bind_endpoint,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids, ids.input,
        binding_payload(root + ".input", root + ".input_channel",
            session_limits));
    lifecycle(backend, SystemCKernelOperation::bind_endpoint,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids, ids.output,
        binding_payload(root + ".output", root + ".output_channel",
            session_limits));
    lifecycle(backend, SystemCKernelOperation::elaborate,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids);
    auto started = lifecycle(backend, SystemCKernelOperation::start,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, ids);
    assert(started.receipt.state == SystemCKernelSessionState::quiescent);
    assert(started.receipt.published);
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

void run_typed_execution(const std::filesystem::path& plugin,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    const SystemCKernelValue& expected, const std::string& factory,
    const std::string& identity, const std::string& root)
{
    fsim::diagnostic::Engine diagnostics;
    auto backend = make_systemc_kernel_session_backend(protocol_limits,
        session_limits, execution_limits, diagnostics);
    assert(backend && diagnostics.diagnostics().empty());
    std::uint64_t sequence = 3000U;
    const auto ids = identities(identity, root, protocol_limits);
    construct_and_start(*backend, plugin, identity, root, sequence,
        protocol_limits, session_limits, factory);

    execute(*backend, SystemCKernelOperation::drain_outputs,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output);
    auto applied = execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input,
        input_payload(expected, execution_limits));
    assert(applied.receipt.current_activity);
    auto advanced = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    assert(advanced.receipt.status == SystemCKernelExecutionStatus::quiescent);

    auto output = execute(*backend, SystemCKernelOperation::drain_outputs,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output);
    assert(output.receipt.samples.size() == 1U);
    assert(output.receipt.samples.front().value.typed == expected);
    auto input = execute(*backend, SystemCKernelOperation::inspect,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input);
    assert(input.receipt.samples.size() == 1U);
    assert(input.receipt.samples.front().value.typed == expected);
    auto snapshot = execute(*backend, SystemCKernelOperation::snapshot,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids);
    assert(snapshot.receipt.samples.size() == 2U);
    assert(std::ranges::all_of(snapshot.receipt.samples,
        [&](const auto& sample) { return sample.value.typed == expected; }));

    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelDirectResultStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(7U, execution_limits));
    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelDirectResultStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output,
        input_payload(expected, execution_limits));
    lifecycle(*backend, SystemCKernelOperation::teardown,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
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
        SystemCKernelOperation::next_activity, SystemCKernelDirectResultStatus::ok,
        { sequence++ }, protocol_limits, execution_limits, ids);
    assert(!initial_activity.receipt.current_activity);
    assert(initial_activity.receipt.future_activity);
    assert(initial_activity.receipt.next_activity_time_fs == 5'000'000U);

    auto initial_output = execute(*backend,
        SystemCKernelOperation::drain_outputs, SystemCKernelDirectResultStatus::ok,
        { sequence++ }, protocol_limits, execution_limits, ids, ids.output);
    assert(initial_output.receipt.samples.size() == 1U);
    assert(initial_output.receipt.samples.front().dirty);
    assert(initial_output.receipt.samples.front().value.bits == 1U);
    auto clean_output = execute(*backend,
        SystemCKernelOperation::drain_outputs, SystemCKernelDirectResultStatus::ok,
        { sequence++ }, protocol_limits, execution_limits, ids, ids.output);
    assert(clean_output.receipt.samples.empty());

    auto applied = execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(7U, execution_limits));
    assert(applied.receipt.current_activity);
    assert(applied.receipt.next_activity_time_fs == 0U);
    auto advanced = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    assert(advanced.receipt.order.time_fs == 0U);
    assert(!advanced.receipt.current_activity);
    assert(advanced.receipt.status == SystemCKernelExecutionStatus::quiescent);

    auto output = execute(*backend, SystemCKernelOperation::drain_outputs,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output);
    assert(output.receipt.samples.size() == 1U);
    assert(output.receipt.samples.front().value.bits == 22U);
    auto input = execute(*backend, SystemCKernelOperation::inspect,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input);
    assert(input.receipt.samples.size() == 1U);
    assert(input.receipt.samples.front().value.bits == 7U);
    auto snapshot = execute(*backend, SystemCKernelOperation::snapshot,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids);
    assert(snapshot.receipt.samples.size() == 2U);
    assert(std::ranges::is_sorted(snapshot.receipt.samples, { },
        &SystemCKernelExecutionSample::order));
    auto report = execute(*backend, SystemCKernelOperation::report,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids);
    assert(report.receipt.detail.find("identity=execution-session")
        != std::string::npos);

    auto timed = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::time, 5'000'000U,
            execution_limits));
    assert(timed.receipt.order.time_fs == 5'000'000U);
    assert(timed.receipt.current_activity);
    assert(timed.receipt.next_activity_time_fs == 5'000'000U);
    auto timed_delta = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    assert(!timed_delta.receipt.current_activity);
    assert(timed_delta.receipt.future_activity);
    assert(timed_delta.receipt.next_activity_time_fs == 10'000'000U);

    auto wrong_direction = execute(*backend,
        SystemCKernelOperation::apply_inputs,
        SystemCKernelDirectResultStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.output, input_payload(9U, execution_limits));
    assert(wrong_direction.receipt.session_state
        == SystemCKernelSessionState::quiescent);
    const SystemCKernelApplyInputsPayload malformed { { 4U, 2U, false } };
    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelDirectResultStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, std::move(malformed));

    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(99U, execution_limits));
    auto paused = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::time, 1U,
            execution_limits));
    assert(paused.receipt.status == SystemCKernelExecutionStatus::paused);
    auto resumed = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::time, 1U,
            execution_limits));
    assert(resumed.receipt.status == SystemCKernelExecutionStatus::quiescent);

    execute(*backend, SystemCKernelOperation::apply_inputs,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(100U, execution_limits));
    auto stopped = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    assert(stopped.receipt.status == SystemCKernelExecutionStatus::stopped);
    execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelDirectResultStatus::rejected, { sequence++ }, protocol_limits,
        execution_limits, ids, { },
        advance_payload(SystemCKernelAdvanceKind::delta, 0U,
            execution_limits));
    auto terminal = lifecycle(*backend, SystemCKernelOperation::teardown,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
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
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        execution_limits, ids, ids.input, input_payload(101U, execution_limits));
    auto failed = execute(*backend, SystemCKernelOperation::advance,
        SystemCKernelDirectResultStatus::failed, { sequence++ }, protocol_limits,
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
        SystemCKernelDirectResultStatus::rejected, { sequence++ }, protocol_limits,
        limits, ids);
    assert(rejected.receipt.code == SystemCKernelExecutionCode::resource);
    lifecycle(*backend, SystemCKernelOperation::teardown,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
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
    assert(systemc_kernel_backend_live_contexts() == 0U);
    run_execution(plugin, protocol_limits, session_limits, execution_limits);
    assert(systemc_kernel_backend_live_contexts() == 0U);
    run_error_rollback(plugin, protocol_limits, session_limits,
        execution_limits);
    run_snapshot_limit(plugin, protocol_limits, session_limits);
    assert(systemc_kernel_backend_live_contexts() == 0U);
    run_typed_execution(value_plugin, protocol_limits, session_limits,
        execution_limits, wide_logic4_value(), "value_root",
        "typed-loopback-session", "typed_loopback_top");
    assert(systemc_kernel_backend_live_contexts() == 0U);
    run_typed_execution(value_plugin, protocol_limits, session_limits,
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
