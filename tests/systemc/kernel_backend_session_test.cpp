// SPDX-License-Identifier: Apache-2.0
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/systemc/kernel_backend_direct.hpp"
#include "fsim/systemc/kernel_backend_session.hpp"
#include "kernel_lifecycle_contract.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>
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
using fsim::systemc::SystemCKernelDirectRequest;
using fsim::systemc::SystemCKernelDirectResult;
using fsim::systemc::SystemCKernelDirectResultStatus;
using fsim::systemc::SystemCKernelProtocolLimits;
using fsim::systemc::SystemCKernelSessionLimits;
using fsim::systemc::SystemCKernelSessionState;
using fsim::systemc::SystemCObjectId;
using fsim::systemc::SystemCSequenceId;

// Test-only selector for the typed lifecycle request alternatives.
enum class SystemCKernelOperation {
    create_session,
    create_object,
    bind_endpoint,
    elaborate,
    start,
    teardown,
};

struct ObjectIds {
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCEndpointId endpoint;
};

struct Response {
    SystemCKernelDirectResultStatus status;
    SystemCKernelLifecycleReceipt receipt;
};

void require_lifecycle_rejection(const SystemCKernelDirectResult& result,
    const SystemCKernelLifecycleCode code,
    const SystemCKernelSessionState state)
{
    assert(result.status == SystemCKernelDirectResultStatus::rejected);
    const auto* receipt
        = std::get_if<SystemCKernelLifecycleReceipt>(&result.receipt);
    assert(receipt != nullptr);
    assert(receipt->code == code);
    assert(receipt->state == state);
}

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

using SessionTestPayload = std::variant<std::monostate,
    SystemCKernelCreateSessionPayload, SystemCKernelCreateObjectPayload,
    SystemCKernelBindEndpointPayload>;

Response transact(fsim::systemc::SystemCKernelBackend& backend,
    const SystemCKernelOperation operation,
    const SystemCKernelDirectResultStatus expected_status,
    const SystemCSequenceId sequence,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCIslandId island = { },
    const SystemCHierarchyId hierarchy = { },
    const SystemCObjectId object = { },
    const SystemCEndpointId endpoint = { },
    SessionTestPayload payload = { })
{
    SystemCKernelDirectRequest request;
    switch (operation) {
    case SystemCKernelOperation::create_session: {
        auto direct_payload = std::get<SystemCKernelCreateSessionPayload>(
            std::move(payload));
        request = fsim::systemc::SystemCKernelCreateSessionRequest {
            island, sequence, std::move(direct_payload)
        };
        break;
    }
    case SystemCKernelOperation::create_object: {
        auto direct_payload = std::get<SystemCKernelCreateObjectPayload>(
            std::move(payload));
        request = fsim::systemc::SystemCKernelCreateObjectRequest {
            island, sequence, hierarchy, object, std::move(direct_payload)
        };
        break;
    }
    case SystemCKernelOperation::bind_endpoint: {
        auto direct_payload = std::get<SystemCKernelBindEndpointPayload>(
            std::move(payload));
        request = fsim::systemc::SystemCKernelBindEndpointRequest {
            island, sequence, hierarchy, object, endpoint,
            std::move(direct_payload)
        };
        break;
    }
    case SystemCKernelOperation::elaborate:
        request = fsim::systemc::SystemCKernelElaborateRequest {
            island, sequence
        };
        break;
    case SystemCKernelOperation::start:
        request = fsim::systemc::SystemCKernelStartRequest { island, sequence };
        break;
    case SystemCKernelOperation::teardown:
        request = fsim::systemc::SystemCKernelTeardownRequest {
            island, sequence
        };
        break;
    default:
        assert(false && "session test only accepts lifecycle operations");
        return { SystemCKernelDirectResultStatus::disconnected, { } };
    }
    static_cast<void>(protocol_limits);
    static_cast<void>(session_limits);
    auto result = backend.request(request);
    assert(result.status == expected_status);
    assert(std::holds_alternative<SystemCKernelLifecycleReceipt>(result.receipt));
    auto receipt = std::get<SystemCKernelLifecycleReceipt>(std::move(result.receipt));
    if (result.status == SystemCKernelDirectResultStatus::ok) {
        assert(receipt.code == SystemCKernelLifecycleCode::none);
    } else {
        assert(receipt.code != SystemCKernelLifecycleCode::none);
        assert(*fsim::systemc::systemc_kernel_lifecycle_diagnostic_code(
                   receipt.code)
            != '\0');
    }
    return { result.status, std::move(receipt) };
}

SystemCKernelCreateSessionPayload session_payload(const std::string& identity,
    const std::filesystem::path& plugin,
    const SystemCKernelSessionLimits& limits)
{
    const auto plugin_path = plugin.string();
    assert(!identity.empty() && identity.size() <= limits.max_identity_bytes);
    assert(!plugin_path.empty()
        && plugin_path.size() <= limits.max_plugin_path_bytes);
    return { identity, plugin_path, 1U };
}

SystemCKernelCreateObjectPayload object_payload(const std::string& hierarchy,
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
    assert(payload.parameters.size() <= limits.max_parameters_per_object);
    assert(hierarchy.size() <= limits.max_name_bytes);
    assert(instance.size() <= limits.max_name_bytes);
    return payload;
}

SystemCKernelBindEndpointPayload binding_payload(const std::string& root,
    const std::string& interface_path,
    const SystemCKernelSessionLimits& limits)
{
    auto payload = SystemCKernelBindEndpointPayload {
        root + ".leaf.input", interface_path
    };
    assert(payload.endpoint_path.size() <= limits.max_name_bytes);
    assert(payload.interface_path.size() <= limits.max_name_bytes);
    return payload;
}

namespace LifecycleContract = fsim::tests::systemc_lifecycle;

LifecycleContract::Result lifecycle_result(
    const SystemCKernelDirectResult& result)
{
    using LifecycleContract::Failure;
    using LifecycleContract::Outcome;
    using LifecycleContract::Phase;
    using LifecycleContract::Result;
    using LifecycleContract::Snapshot;

    const auto* lifecycle_receipt =
        std::get_if<SystemCKernelLifecycleReceipt>(&result.receipt);
    if (lifecycle_receipt == nullptr) {
        return { Outcome::failed, Failure::upstream, Snapshot { } };
    }
    const auto& receipt = *lifecycle_receipt;
    Phase phase = Phase::vacant;
    switch (receipt.state) {
    case SystemCKernelSessionState::vacant:
        phase = Phase::vacant;
        break;
    case SystemCKernelSessionState::constructing:
        phase = Phase::constructing;
        break;
    case SystemCKernelSessionState::elaborated:
        phase = Phase::elaborated;
        break;
    case SystemCKernelSessionState::quiescent:
        phase = Phase::started;
        break;
    case SystemCKernelSessionState::terminal:
        phase = Phase::terminal;
        break;
    case SystemCKernelSessionState::failed:
        phase = Phase::failed;
        break;
    }

    Outcome outcome = Outcome::success;
    switch (result.status) {
    case SystemCKernelDirectResultStatus::ok:
        outcome = Outcome::success;
        break;
    case SystemCKernelDirectResultStatus::rejected:
        outcome = Outcome::rejected;
        break;
    case SystemCKernelDirectResultStatus::failed:
        outcome = Outcome::failed;
        break;
    case SystemCKernelDirectResultStatus::disconnected:
        outcome = Outcome::failed;
        break;
    default:
        outcome = Outcome::failed;
        break;
    }

    Failure failure = Failure::none;
    switch (receipt.code) {
    case SystemCKernelLifecycleCode::none:
        failure = Failure::none;
        break;
    case SystemCKernelLifecycleCode::session_state:
        failure = Failure::state;
        break;
    case SystemCKernelLifecycleCode::payload:
        failure = Failure::payload;
        break;
    case SystemCKernelLifecycleCode::resource:
        failure = Failure::resource;
        break;
    case SystemCKernelLifecycleCode::upstream:
        failure = Failure::upstream;
        break;
    }
    return Result { outcome, failure,
        Snapshot { phase, receipt.published, receipt.staged_objects,
            receipt.staged_bindings, receipt.published_objects,
            receipt.context_generation } };
}

class DirectLifecycleDriver final : public LifecycleContract::Driver {
public:
    DirectLifecycleDriver(
        std::unique_ptr<fsim::systemc::SystemCKernelBackend> backend,
        const SystemCKernelProtocolLimits& protocol_limits,
        std::uint64_t& sequence)
        : backend_ { std::move(backend) }
        , protocol_limits_ { protocol_limits }
        , sequence_ { sequence }
    {
    }

    [[nodiscard]] LifecycleContract::Result create_session(
        const LifecycleContract::Session& request) override
    {
        identity_ = request.identity;
        island_ = island_id(identity_, protocol_limits_);
        return invoke(fsim::systemc::SystemCKernelCreateSessionRequest {
            island_, { sequence_++ }, { identity_, request.plugin.string(), 1U }
        });
    }

    [[nodiscard]] LifecycleContract::Result create_object(
        const LifecycleContract::Object& request) override
    {
        ids_ = object_ids(island_, request.hierarchy_path,
            request.object_path, request.object_path + ".leaf.input",
            protocol_limits_);
        SystemCKernelCreateObjectPayload payload;
        payload.hierarchy_path = request.hierarchy_path;
        payload.object_path = request.object_path;
        payload.factory = request.factory;
        payload.instance = request.instance;
        payload.parameters.reserve(request.parameters.size());
        for (const auto& [name, value] : request.parameters) {
            payload.parameters.push_back({ name, value });
        }
        return invoke(fsim::systemc::SystemCKernelCreateObjectRequest {
            island_, { sequence_++ }, ids_.hierarchy, ids_.object,
            std::move(payload)
        });
    }

    [[nodiscard]] LifecycleContract::Result bind_endpoint(
        const LifecycleContract::EndpointBinding& request) override
    {
        fsim::diagnostic::Engine diagnostics;
        const auto endpoint = fsim::systemc::make_systemc_endpoint_id(
            ids_.object, request.endpoint_path, protocol_limits_, diagnostics);
        assert(endpoint && diagnostics.diagnostics().empty());
        return invoke(fsim::systemc::SystemCKernelBindEndpointRequest {
            island_, { sequence_++ }, ids_.hierarchy, ids_.object, *endpoint,
            { request.endpoint_path, request.interface_path }
        });
    }

    [[nodiscard]] LifecycleContract::Result elaborate() override
    {
        return invoke(fsim::systemc::SystemCKernelElaborateRequest {
            island_, { sequence_++ }
        });
    }

    [[nodiscard]] LifecycleContract::Result start() override
    {
        return invoke(fsim::systemc::SystemCKernelStartRequest {
            island_, { sequence_++ }
        });
    }

    [[nodiscard]] LifecycleContract::Result shutdown() override
    {
        return invoke(fsim::systemc::SystemCKernelTeardownRequest {
            island_, { sequence_++ }
        });
    }

private:
    [[nodiscard]] LifecycleContract::Result invoke(
        const SystemCKernelDirectRequest& request)
    {
        const auto result = backend_->request(request);
        return lifecycle_result(result);
    }

    std::unique_ptr<fsim::systemc::SystemCKernelBackend> backend_;
    const SystemCKernelProtocolLimits& protocol_limits_;
    std::uint64_t& sequence_;
    std::string identity_;
    SystemCIslandId island_;
    ObjectIds ids_;
};

class SessionPluginProbe final : public LifecycleContract::Probe {
public:
    using Reset = void (*)() noexcept;
    using Counter = std::size_t (*)(std::uint32_t) noexcept;

    SessionPluginProbe(const Reset reset, const Counter counter)
        : reset_ { reset }
        , counter_ { counter }
    {
    }

    void reset() override { reset_(); }

    [[nodiscard]] std::size_t count(
        const LifecycleContract::Counter counter) const override
    {
        switch (counter) {
        case LifecycleContract::Counter::live_marker:
            return counter_(0U);
        case LifecycleContract::Counter::constructed_roots:
            return counter_(1U);
        case LifecycleContract::Counter::destroyed_roots:
            return counter_(2U);
        case LifecycleContract::Counter::before_elaboration:
            return counter_(3U);
        case LifecycleContract::Counter::end_elaboration:
            return counter_(4U);
        case LifecycleContract::Counter::start:
            return counter_(5U);
        case LifecycleContract::Counter::initial_evaluation:
            return counter_(6U);
        case LifecycleContract::Counter::end:
            return counter_(7U);
        case LifecycleContract::Counter::observed_width:
            return counter_(8U);
        }
        return 0U;
    }

    [[nodiscard]] std::size_t live_contexts() const override
    {
        return fsim::systemc::systemc_kernel_backend_live_contexts();
    }

private:
    Reset reset_ { };
    Counter counter_ { };
};

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
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, island, { }, { }, { },
        session_payload(identity, plugin, session_limits));
    assert(created.receipt.state == SystemCKernelSessionState::constructing);
    assert(!created.receipt.published);
    assert(created.receipt.context_generation != 0U);

    auto object = transact(backend, SystemCKernelOperation::create_object,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, island, ids.hierarchy, ids.object, { },
        object_payload(hierarchy_path, root, width, false, session_limits));
    assert(object.receipt.staged_objects == 1U);
    assert(object.receipt.published_objects == 0U);

    auto binding = transact(backend, SystemCKernelOperation::bind_endpoint,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, island, ids.hierarchy, ids.object, ids.endpoint,
        binding_payload(root, root + ".channel", session_limits));
    assert(binding.receipt.staged_bindings == 1U);

    auto elaborated = transact(backend, SystemCKernelOperation::elaborate,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, island);
    assert(elaborated.receipt.state == SystemCKernelSessionState::elaborated);
    assert(!elaborated.receipt.published);

    auto started = transact(backend, SystemCKernelOperation::start,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
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
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);

    fsim::diagnostic::Engine diagnostics;
    auto backend = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(backend && diagnostics.diagnostics().empty());
    const auto sequence_island = island_id("invalid-direct-identity",
        protocol_limits);
    const auto valid_identity_payload = session_payload(
        "invalid-direct-identity", plugin, session_limits);
    const SystemCKernelDirectRequest invalid_island_request {
        fsim::systemc::SystemCKernelCreateSessionRequest {
            { }, { 1U }, valid_identity_payload
        }
    };
    require_lifecycle_rejection(backend->request(invalid_island_request),
        SystemCKernelLifecycleCode::payload,
        SystemCKernelSessionState::vacant);
    const SystemCKernelDirectRequest zero_sequence_request {
        fsim::systemc::SystemCKernelCreateSessionRequest {
            sequence_island, { }, valid_identity_payload
        }
    };
    require_lifecycle_rejection(backend->request(zero_sequence_request),
        SystemCKernelLifecycleCode::payload,
        SystemCKernelSessionState::vacant);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);

    const auto invalid_island = island_id("invalid-direct-session",
        protocol_limits);
    const SystemCKernelDirectRequest invalid_session_request {
        fsim::systemc::SystemCKernelCreateSessionRequest {
            invalid_island, { 1U }, { "", plugin.string(), 1U }
        }
    };
    require_lifecycle_rejection(backend->request(invalid_session_request),
        SystemCKernelLifecycleCode::payload,
        SystemCKernelSessionState::vacant);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);

    std::uint64_t contract_sequence = 1U;
    SessionPluginProbe contract_probe { reset, counter };
    auto make_contract_driver = [&]() {
        diagnostics = { };
        auto contract_backend = fsim::systemc::make_systemc_kernel_session_backend(
            protocol_limits, session_limits, diagnostics);
        assert(contract_backend && diagnostics.diagnostics().empty());
        return std::make_unique<DirectLifecycleDriver>(
            std::move(contract_backend), protocol_limits, contract_sequence);
    };
    LifecycleContract::run_contract(
        make_contract_driver, contract_probe, plugin);

    reset();
    std::uint64_t sequence = 2U;
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
    const auto primary_ids = object_ids(primary_island, "primary-roots",
        "top", "top.leaf.input", protocol_limits);
    const auto foreign_island = island_id("foreign-session", protocol_limits);
    const SystemCKernelDirectRequest foreign_island_request {
        fsim::systemc::SystemCKernelElaborateRequest {
            foreign_island, { sequence++ }
        }
    };
    require_lifecycle_rejection(backend->request(foreign_island_request),
        SystemCKernelLifecycleCode::session_state,
        SystemCKernelSessionState::quiescent);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 1U);

    fsim::systemc::SystemCKernelValue malformed_typed_value;
    malformed_typed_value.width = 1U;
    fsim::systemc::SystemCKernelScalarValue malformed_value;
    malformed_value.typed = std::move(malformed_typed_value);
    const SystemCKernelDirectRequest malformed_value_request {
        fsim::systemc::SystemCKernelApplyInputsRequest {
            primary_island, { sequence++ }, primary_ids.object,
            primary_ids.endpoint, { std::move(malformed_value) }
        }
    };
    const auto malformed_value_result = backend->request(malformed_value_request);
    assert(malformed_value_result.status
        == SystemCKernelDirectResultStatus::rejected);
    const auto* malformed_value_receipt =
        std::get_if<fsim::systemc::SystemCKernelExecutionReceipt>(
            &malformed_value_result.receipt);
    assert(malformed_value_receipt != nullptr);
    assert(malformed_value_receipt->code
        == fsim::systemc::SystemCKernelExecutionCode::payload);
    assert(malformed_value_receipt->session_state
        == SystemCKernelSessionState::quiescent);
    assert(malformed_value_receipt->published);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 1U);

    auto repeated_start = transact(*backend, SystemCKernelOperation::start,
        SystemCKernelDirectResultStatus::rejected, { sequence++ }, protocol_limits,
        session_limits, primary_island);
    assert(repeated_start.receipt.state
        == SystemCKernelSessionState::quiescent);
    auto terminal = transact(*backend, SystemCKernelOperation::teardown,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
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
        SystemCKernelOperation::teardown, SystemCKernelDirectResultStatus::ok,
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
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, failing_island, { }, { }, { },
        session_payload("failing-session", plugin, session_limits));
    auto failed_object = transact(*failing,
        SystemCKernelOperation::create_object,
        SystemCKernelDirectResultStatus::failed, { sequence++ }, protocol_limits,
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
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, repeated_island, { }, { }, { },
        session_payload("repeated-session", plugin, session_limits));
    const auto repeated_payload = object_payload("repeat-roots",
        "repeat_top", 5, false, session_limits);
    transact(*repeated, SystemCKernelOperation::create_object,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, repeated_island, repeated_ids.hierarchy,
        repeated_ids.object, { }, repeated_payload);
    auto duplicate = transact(*repeated,
        SystemCKernelOperation::create_object,
        SystemCKernelDirectResultStatus::failed, { sequence++ }, protocol_limits,
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
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, bad_binding_island, { }, { }, { }, session_payload("bad-binding-session", plugin, session_limits));
    transact(*bad_binding, SystemCKernelOperation::create_object,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, bad_binding_island, bad_binding_ids.hierarchy,
        bad_binding_ids.object, { }, object_payload("bad-binding-roots", "bad_binding_top", 11, false, session_limits));
    auto rejected_binding = transact(*bad_binding,
        SystemCKernelOperation::bind_endpoint,
        SystemCKernelDirectResultStatus::failed, { sequence++ }, protocol_limits,
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
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, unbound_island, { }, { }, { },
        session_payload("unbound-session", plugin, session_limits));
    transact(*unbound, SystemCKernelOperation::create_object,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, unbound_island, unbound_ids.hierarchy,
        unbound_ids.object, { }, object_payload("unbound-roots", "unbound_top", 7, false, session_limits));
    auto failed_elaboration = transact(*unbound,
        SystemCKernelOperation::elaborate,
        SystemCKernelDirectResultStatus::failed, { sequence++ }, protocol_limits,
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
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, repeated_session_island, { }, { }, { },
        repeated_session_bytes);
    auto rejected_session = transact(*repeated_session,
        SystemCKernelOperation::create_session,
        SystemCKernelDirectResultStatus::rejected, { sequence++ }, protocol_limits,
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
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, left_island);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 1U);
    transact(*right, SystemCKernelOperation::teardown,
        SystemCKernelDirectResultStatus::ok, { sequence++ }, protocol_limits,
        session_limits, right_island);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);
    assert(counter(0U) == 0U);
    assert(counter(1U) == 2U && counter(2U) == 2U);
    assert(counter(3U) == 2U && counter(4U) == 2U);
    assert(counter(5U) == 2U && counter(6U) == 2U);
    assert(counter(7U) == 2U);

    diagnostics = { };
    SystemCKernelSessionLimits large_detail_limits = session_limits;
    large_detail_limits.max_detail_bytes = session_limits.max_detail_bytes * 2U;
    auto large_detail = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, large_detail_limits, diagnostics);
    assert(large_detail && diagnostics.diagnostics().empty());
    large_detail->close();

    diagnostics = { };
    auto missing = fsim::systemc::make_systemc_kernel_session_backend(
        protocol_limits, session_limits, diagnostics);
    assert(missing);
    const auto missing_island = island_id("missing-plugin", protocol_limits);
    auto missing_response = transact(*missing,
        SystemCKernelOperation::create_session,
        SystemCKernelDirectResultStatus::failed, { sequence++ }, protocol_limits,
        session_limits, missing_island, { }, { }, { }, session_payload("missing-plugin", plugin.string() + ".missing", session_limits));
    assert(missing_response.receipt.state
        == SystemCKernelSessionState::vacant);
    assert(fsim::systemc::systemc_kernel_backend_live_contexts() == 0U);
    return 0;
}
