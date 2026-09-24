// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_session.hpp"

#include "fsim/systemc/accellera.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/systemc/kernel_backend_direct.hpp"
#include "fsim/systemc/kernel_backend_execution.hpp"
#include "context_activation.hpp"
#include "kernel_backend_execution_internal.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fsim::systemc {
namespace {

    constexpr std::uint64_t kMaximumTimeResolutionFs = 1'000'000'000'000'000ULL;

    std::atomic_size_t gLiveSystemCContexts { 0U };
    std::atomic_uint64_t gNextContextGeneration { 1U };

    bool report_payload_error(
        diagnostic::Engine& diagnostics, const std::string_view message)
    {
        diagnostics.error("FSIM-SC-S002", std::string { message });
        return false;
    }

    bool report_resource_error(
        diagnostic::Engine& diagnostics, const std::string_view message)
    {
        diagnostics.error("FSIM-SC-S003", std::string { message });
        return false;
    }

    bool valid_limits(const SystemCKernelSessionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        constexpr auto maximum = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
        if (limits.max_plugin_path_bytes == 0U
            || limits.max_identity_bytes == 0U || limits.max_name_bytes == 0U
            || limits.max_objects == 0U || limits.max_bindings == 0U
            || limits.max_parameters_per_object == 0U
            || limits.max_detail_bytes == 0U
            || limits.max_plugin_path_bytes > maximum
            || limits.max_identity_bytes > maximum
            || limits.max_name_bytes > maximum || limits.max_objects > maximum
            || limits.max_bindings > maximum
            || limits.max_parameters_per_object > maximum
            || limits.max_detail_bytes > maximum) {
            return report_resource_error(
                diagnostics, "SystemC session limits must be nonzero 32-bit values");
        }
        return true;
    }

    bool valid_text(const std::string_view value, const std::size_t maximum,
        diagnostic::Engine& diagnostics, const std::string_view role)
    {
        if (value.empty() || value.size() > maximum
            || value.find('\0') != std::string_view::npos) {
            return report_payload_error(diagnostics,
                std::string { "SystemC session " } + std::string { role }
                    + " is empty, oversized, or contains NUL");
        }
        return true;
    }

    bool valid_root_name(
        const std::string_view value, diagnostic::Engine& diagnostics)
    {
        if (value.find('.') != std::string_view::npos
            || value.find('/') != std::string_view::npos
            || value.find('\\') != std::string_view::npos) {
            return report_payload_error(diagnostics,
                "SystemC root instance must be one canonical hierarchy component");
        }
        return true;
    }

    bool valid_create_session_payload(
        const SystemCKernelCreateSessionPayload& payload,
        const SystemCKernelSessionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        return valid_limits(limits, diagnostics)
            && valid_text(payload.canonical_identity, limits.max_identity_bytes,
                diagnostics, "canonical identity")
            && valid_text(payload.plugin_path, limits.max_plugin_path_bytes,
                diagnostics, "plug-in path")
            && (payload.time_resolution_fs > 0U
                        && payload.time_resolution_fs <= kMaximumTimeResolutionFs
                    ? true
                    : report_payload_error(diagnostics,
                          "SystemC session time resolution is outside the governed range"));
    }

    bool valid_create_object_payload(
        const SystemCKernelCreateObjectPayload& payload,
        const SystemCKernelSessionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (!valid_limits(limits, diagnostics)
            || !valid_text(payload.hierarchy_path, limits.max_name_bytes,
                diagnostics, "hierarchy path")
            || !valid_text(payload.object_path, limits.max_name_bytes,
                diagnostics, "object path")
            || !valid_text(payload.factory, limits.max_name_bytes, diagnostics,
                "factory name")
            || !valid_text(payload.instance, limits.max_name_bytes, diagnostics,
                "instance name")
            || !valid_root_name(payload.instance, diagnostics)) {
            return false;
        }
        if (payload.object_path != payload.instance) {
            return report_payload_error(diagnostics,
                "SystemC root object path must equal its upstream instance name");
        }
        if (payload.parameters.size() > limits.max_parameters_per_object) {
            return report_resource_error(diagnostics,
                "SystemC object exceeds its construction-parameter limit");
        }
        std::set<std::string_view> names;
        for (const auto& parameter : payload.parameters) {
            if (!valid_text(parameter.name, limits.max_name_bytes, diagnostics,
                    "construction parameter")) {
                return false;
            }
            if (!names.insert(parameter.name).second) {
                return report_payload_error(diagnostics,
                    "SystemC object repeats a construction parameter");
            }
        }
        return true;
    }

    bool valid_bind_payload(const SystemCKernelBindEndpointPayload& payload,
        const SystemCKernelSessionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        return valid_limits(limits, diagnostics)
            && valid_text(payload.endpoint_path, limits.max_name_bytes,
                diagnostics, "endpoint path")
            && valid_text(payload.interface_path, limits.max_name_bytes,
                diagnostics, "interface path");
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

    bool known_advance_kind(const SystemCKernelAdvanceKind kind) noexcept
    {
        return kind == SystemCKernelAdvanceKind::delta
            || kind == SystemCKernelAdvanceKind::time;
    }

    using ContextActivation = detail::ContextActivation;

    struct RootRecord {
        SystemCHierarchyId hierarchy;
        SystemCObjectId object;
        std::string object_path;
        fsim_sc_handle_v1 registry_handle { };
    };

    struct EndpointRecord {
        SystemCEndpointId endpoint;
        SystemCObjectId object;
        std::string endpoint_path;
        std::string interface_path;
        backend_scalar_endpoint* scalar { };
        backend_value_endpoint* typed { };
        std::optional<SystemCKernelScalarValue> last_value;
    };

    class SystemCKernelSessionBackend final : public SystemCKernelBackend {
    public:
        SystemCKernelSessionBackend(SystemCKernelProtocolLimits protocol_limits,
            SystemCKernelSessionLimits session_limits,
            SystemCKernelExecutionLimits execution_limits = { })
            : protocol_limits_ { protocol_limits }
            , session_limits_ { session_limits }
            , execution_limits_ { execution_limits }
        {
        }

        ~SystemCKernelSessionBackend() override
        {
            close();
        }

        [[nodiscard]] SystemCKernelDirectResult request(
            const SystemCKernelDirectRequest& direct_request) noexcept override
        {
            try {
                std::lock_guard lock { mutex_ };
                try {
                    return std::visit(
                        [this](const auto& operation) {
                            return dispatch(operation);
                        }, direct_request);
                } catch (...) {
                    fail_and_rollback();
                    return { SystemCKernelDirectResultStatus::disconnected, { } };
                }
            } catch (...) {
                return { SystemCKernelDirectResultStatus::disconnected, { } };
            }
        }

        void close() noexcept override
        {
            try {
                std::lock_guard lock { mutex_ };
                teardown(false);
            } catch (...) {
            }
        }

    private:
        template <typename Request>
        [[nodiscard]] SystemCKernelDirectResult dispatch(
            const Request& request)
        {
            using RequestType = std::remove_cvref_t<Request>;
            constexpr bool execution_request =
                std::is_same_v<RequestType, SystemCKernelApplyInputsRequest>
                || std::is_same_v<RequestType, SystemCKernelAdvanceRequest>
                || std::is_same_v<RequestType, SystemCKernelNextActivityRequest>
                || std::is_same_v<RequestType, SystemCKernelDrainOutputsRequest>
                || std::is_same_v<RequestType, SystemCKernelReportRequest>
                || std::is_same_v<RequestType, SystemCKernelInspectRequest>
                || std::is_same_v<RequestType, SystemCKernelSnapshotRequest>;
            if (!request.island.valid() || !request.sequence.valid()) {
                if constexpr (execution_request) {
                    return reject_execution(
                        "SystemC request has an invalid island or sequence identity",
                        SystemCKernelExecutionCode::payload);
                } else {
                    return respond(SystemCKernelDirectResultStatus::rejected,
                        "SystemC request has an invalid island or sequence identity",
                        SystemCKernelLifecycleCode::payload);
                }
            }
            if (session_state_ != SystemCKernelSessionState::vacant
                && request.island != island_) {
                if constexpr (execution_request) {
                    return reject_execution(
                        "request island does not own this backend session");
                } else {
                    return respond(SystemCKernelDirectResultStatus::rejected,
                        "request island does not own this backend session",
                        SystemCKernelLifecycleCode::session_state);
                }
            }
            if constexpr (std::is_same_v<RequestType,
                              SystemCKernelCreateSessionRequest>) {
                return create_session(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelCreateObjectRequest>) {
                return create_object(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelBindEndpointRequest>) {
                return bind_endpoint(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelElaborateRequest>) {
                return elaborate(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelStartRequest>) {
                return start(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelApplyInputsRequest>) {
                return apply_inputs(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelAdvanceRequest>) {
                return advance(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelNextActivityRequest>) {
                return next_activity(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelDrainOutputsRequest>) {
                return drain_outputs(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelReportRequest>) {
                return report(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelInspectRequest>) {
                return inspect(request);
            } else if constexpr (std::is_same_v<RequestType,
                                     SystemCKernelSnapshotRequest>) {
                return snapshot(request);
            } else {
                static_assert(std::is_same_v<RequestType,
                    SystemCKernelTeardownRequest>);
                teardown(true);
                return respond(SystemCKernelDirectResultStatus::ok,
                    "SystemC session reached terminal teardown");
            }
        }

        [[nodiscard]] SystemCKernelDirectResult create_session(
            const SystemCKernelCreateSessionRequest& request)
        {
            if (session_state_ != SystemCKernelSessionState::vacant) {
                fail_and_rollback();
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "SystemC session construction cannot be repeated",
                    SystemCKernelLifecycleCode::session_state);
            }
            diagnostic::Engine diagnostics;
            const auto& payload = request.payload;
            if (!valid_create_session_payload(
                    payload, session_limits_, diagnostics)) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "invalid SystemC session-construction payload",
                    SystemCKernelLifecycleCode::payload);
            }
            auto expected_island = make_systemc_island_id(
                payload.canonical_identity, protocol_limits_, diagnostics);
            if (!expected_island || *expected_island != request.island) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "SystemC session identity does not match its island ID",
                    SystemCKernelLifecycleCode::session_state);
            }

            std::unique_ptr<sc_core::sc_simcontext> candidate;
            try {
                candidate = std::make_unique<sc_core::sc_simcontext>();
            } catch (...) {
                return respond(SystemCKernelDirectResultStatus::failed,
                    "cannot allocate the upstream SystemC context",
                    SystemCKernelLifecycleCode::upstream);
            }
            std::string error;
            std::unique_ptr<HierarchyRegistry> candidate_registry;
            try {
                ContextActivation active { candidate.get() };
                sc_core::sc_set_time_resolution(
                    static_cast<double>(payload.time_resolution_fs),
                    sc_core::SC_FS);
                candidate_registry = HierarchyRegistry::load(
                    std::filesystem::path { payload.plugin_path }, error);
            } catch (const std::exception& exception) {
                error = exception.what();
            } catch (...) {
                error = "unknown upstream context-construction failure";
            }
            if (!candidate_registry) {
                candidate.reset();
                return respond(SystemCKernelDirectResultStatus::failed,
                    error.empty() ? "cannot load the SystemC session plug-in"
                                  : error,
                    SystemCKernelLifecycleCode::upstream);
            }

            island_ = request.island;
            canonical_identity_ = payload.canonical_identity;
            time_resolution_fs_ = payload.time_resolution_fs;
            context_generation_ = gNextContextGeneration.fetch_add(1U, std::memory_order_relaxed);
            context_ = std::move(candidate);
            registry_ = std::move(candidate_registry);
            session_state_ = SystemCKernelSessionState::constructing;
            gLiveSystemCContexts.fetch_add(1U, std::memory_order_relaxed);
            return respond(SystemCKernelDirectResultStatus::ok,
                "SystemC session owns one fresh upstream context");
        }

        [[nodiscard]] SystemCKernelDirectResult create_object(
            const SystemCKernelCreateObjectRequest& request)
        {
            if (session_state_ != SystemCKernelSessionState::constructing
                || context_ == nullptr || registry_ == nullptr) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "SystemC objects can be created only during construction",
                    SystemCKernelLifecycleCode::session_state);
            }
            diagnostic::Engine diagnostics;
            const auto& payload = request.payload;
            if (!valid_create_object_payload(
                    payload, session_limits_, diagnostics)) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "invalid SystemC object-construction payload",
                    SystemCKernelLifecycleCode::payload);
            }
            if (roots_.size() >= session_limits_.max_objects) {
                return fail_mutation(
                    "SystemC session object limit is exhausted",
                    SystemCKernelLifecycleCode::resource);
            }
            const auto hierarchy = make_systemc_hierarchy_id(island_,
                payload.hierarchy_path, protocol_limits_, diagnostics);
            const auto object = hierarchy
                ? make_systemc_object_id(*hierarchy, payload.object_path,
                      protocol_limits_, diagnostics)
                : std::nullopt;
            if (!hierarchy || !object || *hierarchy != request.hierarchy
                || *object != request.object) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "SystemC object paths do not match their typed identities",
                    SystemCKernelLifecycleCode::session_state);
            }
            if (roots_.contains(*object)
                || std::ranges::any_of(roots_, [&](const auto& entry) {
                       return entry.second.object_path == payload.object_path;
                   })) {
                return fail_mutation(
                    "SystemC object construction cannot be repeated",
                    SystemCKernelLifecycleCode::session_state);
            }

            std::vector<std::pair<std::string, std::int64_t>> parameters;
            parameters.reserve(payload.parameters.size());
            for (const auto& parameter : payload.parameters) {
                parameters.emplace_back(parameter.name, parameter.value);
            }
            std::string error;
            std::optional<ModuleDescription> description;
            try {
                ContextActivation active { context_.get() };
                description = registry_->instantiate(payload.factory,
                    payload.instance, 0U, parameters, error);
                const auto* native = sc_core::sc_find_object(
                    payload.object_path.c_str());
                if (description && native == nullptr) {
                    error = "factory did not publish its root into the owned context";
                    description.reset();
                } else if (description
                    && dynamic_cast<const sc_core::sc_module*>(native) == nullptr) {
                    error = "factory root is not an upstream sc_module";
                    description.reset();
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            } catch (...) {
                error = "unknown native SystemC factory failure";
            }
            if (!description) {
                return fail_mutation(
                    error.empty() ? "native SystemC factory rejected construction"
                                  : error);
            }
            roots_.emplace(*object,
                RootRecord { *hierarchy, *object, payload.object_path,
                    description->handle });
            return respond(SystemCKernelDirectResultStatus::ok,
                "SystemC object is staged in the owned upstream context");
        }

        [[nodiscard]] SystemCKernelDirectResult bind_endpoint(
            const SystemCKernelBindEndpointRequest& request)
        {
            if (session_state_ != SystemCKernelSessionState::constructing
                || context_ == nullptr) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "SystemC endpoints can be bound only during construction",
                    SystemCKernelLifecycleCode::session_state);
            }
            diagnostic::Engine diagnostics;
            const auto& payload = request.payload;
            if (!valid_bind_payload(payload, session_limits_, diagnostics)) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "invalid SystemC endpoint-binding payload",
                    SystemCKernelLifecycleCode::payload);
            }
            const auto root = roots_.find(request.object);
            if (root == roots_.end()
                || root->second.hierarchy != request.hierarchy) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "SystemC endpoint references an unknown staged object",
                    SystemCKernelLifecycleCode::session_state);
            }
            const auto endpoint = make_systemc_endpoint_id(root->second.object,
                payload.endpoint_path, protocol_limits_, diagnostics);
            if (!endpoint || *endpoint != request.endpoint) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "SystemC endpoint path does not match its typed identity",
                    SystemCKernelLifecycleCode::session_state);
            }
            if (endpoints_.contains(*endpoint)
                || endpoints_.size() >= session_limits_.max_bindings) {
                return fail_mutation(
                    "SystemC endpoint binding is repeated or exceeds its limit",
                    endpoints_.contains(*endpoint)
                        ? SystemCKernelLifecycleCode::session_state
                        : SystemCKernelLifecycleCode::resource);
            }
            if (!payload.endpoint_path.starts_with(
                    root->second.object_path + ".")) {
                return fail_mutation(
                    "SystemC endpoint is outside its owning root hierarchy",
                    SystemCKernelLifecycleCode::session_state);
            }

            std::string error;
            backend_scalar_endpoint* scalar { };
            backend_value_endpoint* typed { };
            try {
                ContextActivation active { context_.get() };
                auto* endpoint_object = sc_core::sc_find_object(
                    payload.endpoint_path.c_str());
                auto* interface_object = sc_core::sc_find_object(
                    payload.interface_path.c_str());
                auto* port = dynamic_cast<sc_core::sc_port_base*>(endpoint_object);
                auto* bindable = dynamic_cast<backend_bindable_endpoint*>(endpoint_object);
                scalar = dynamic_cast<backend_scalar_endpoint*>(endpoint_object);
                typed = dynamic_cast<backend_value_endpoint*>(endpoint_object);
                auto* target = dynamic_cast<sc_core::sc_interface*>(
                    interface_object);
                if (target == nullptr) {
                    if (auto* exported = dynamic_cast<sc_core::sc_export_base*>(
                            interface_object)) {
                        target = exported->get_interface();
                    }
                }
                if (port == nullptr || bindable == nullptr || target == nullptr
                    || !bindable->bind_backend_interface(*target)) {
                    error = "named upstream port is not bound to the named interface";
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            } catch (...) {
                error = "unknown native SystemC binding failure";
            }
            if (!error.empty()) {
                return fail_mutation(error);
            }
            endpoints_.emplace(*endpoint,
                EndpointRecord { *endpoint, root->second.object,
                    payload.endpoint_path, payload.interface_path, scalar,
                    typed, { } });
            return respond(SystemCKernelDirectResultStatus::ok,
                "SystemC endpoint binding is staged and identity-checked");
        }

        [[nodiscard]] SystemCKernelDirectResult elaborate(
            const SystemCKernelElaborateRequest&)
        {
            if (session_state_ != SystemCKernelSessionState::constructing
                || context_ == nullptr || roots_.empty()) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "SystemC elaboration requires a nonempty constructing session",
                    SystemCKernelLifecycleCode::session_state);
            }
            std::string error;
            try {
                ContextActivation active { context_.get() };
                context_->elaborate();
                if (!context_->elaboration_done()
                    || context_->sim_status() != sc_core::SC_SIM_OK) {
                    error = "upstream SystemC kernel did not complete elaboration";
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            } catch (...) {
                error = "unknown upstream SystemC elaboration failure";
            }
            if (!error.empty()) {
                return fail_mutation(error);
            }
            session_state_ = SystemCKernelSessionState::elaborated;
            return respond(SystemCKernelDirectResultStatus::ok,
                "upstream SystemC elaboration completed without publication");
        }

        [[nodiscard]] SystemCKernelDirectResult start(
            const SystemCKernelStartRequest&)
        {
            if (session_state_ != SystemCKernelSessionState::elaborated
                || context_ == nullptr) {
                return respond(SystemCKernelDirectResultStatus::rejected,
                    "SystemC start requires a completed elaboration",
                    SystemCKernelLifecycleCode::session_state);
            }
            std::string error;
            try {
                ContextActivation active { context_.get() };
                context_->initialize(false);
                if (context_->pending_activity_at_current_time()) {
                    context_->simulate(sc_core::SC_ZERO_TIME);
                }
                if (context_->sim_status() != sc_core::SC_SIM_OK
                    || context_->pending_activity_at_current_time()) {
                    error = "upstream SystemC kernel did not reach zero-time quiescence";
                }
            } catch (const std::exception& exception) {
                error = exception.what();
            } catch (...) {
                error = "unknown upstream SystemC start failure";
            }
            if (!error.empty()) {
                return fail_mutation(error);
            }
            session_state_ = SystemCKernelSessionState::quiescent;
            published_ = true;
            {
                ContextActivation active { context_.get() };
                last_observation_ = detail::systemc_kernel_observe_native(
                    *context_, time_resolution_fs_, error);
                if (last_observation_
                    && !refresh_outputs(*last_observation_, true, error)) {
                    last_observation_.reset();
                }
            }
            if (!last_observation_) {
                return fail_mutation(
                    error.empty() ? "cannot establish initial execution state"
                                  : error);
            }
            return respond(SystemCKernelDirectResultStatus::ok,
                "SystemC session started and reached zero-time quiescence");
        }

        [[nodiscard]] bool execution_available() const noexcept
        {
            return session_state_ == SystemCKernelSessionState::quiescent
                && published_ && context_ != nullptr;
        }

        [[nodiscard]] SystemCKernelExecutionOrder make_execution_order(
            const detail::SystemCKernelNativeObservation& observation,
            const SystemCAccelleraRegion region)
        {
            if (next_result_sequence_ == 0U) {
                next_result_sequence_ = 1U;
            }
            return { observation.time_fs, observation.delta, region, island_,
                { next_result_sequence_++ } };
        }

        [[nodiscard]] std::optional<SystemCKernelExecutionSample> sample_endpoint(
            const EndpointRecord& endpoint,
            const detail::SystemCKernelNativeObservation& observation,
            const SystemCAccelleraRegion region, const bool dirty,
            std::string& error)
        {
            SystemCKernelScalarValue value;
            if (endpoint.typed != nullptr) {
                value.typed = detail::systemc_kernel_sample_value(
                    *endpoint.typed, error);
                if (!value.typed) {
                    return std::nullopt;
                }
            } else if (endpoint.scalar != nullptr) {
                auto scalar = detail::systemc_kernel_sample_scalar(
                    *endpoint.scalar, error);
                if (!scalar) {
                    return std::nullopt;
                }
                value = *scalar;
            } else {
                error = "SystemC endpoint has no bounded value adapter";
                return std::nullopt;
            }
            return SystemCKernelExecutionSample { endpoint.endpoint,
                make_execution_order(observation, region), value, dirty };
        }

        [[nodiscard]] bool refresh_outputs(
            const detail::SystemCKernelNativeObservation& observation,
            const bool mark_dirty, std::string& error)
        {
            for (auto& [identity, endpoint] : endpoints_) {
                static_cast<void>(identity);
                const auto direction = endpoint.typed != nullptr
                    ? endpoint.typed->backend_direction()
                    : endpoint.scalar != nullptr
                    ? endpoint.scalar->backend_direction()
                    : backend_endpoint_direction::input;
                if (direction != backend_endpoint_direction::output) {
                    continue;
                }
                auto value = sample_endpoint(endpoint, observation,
                    SystemCAccelleraRegion::update, mark_dirty, error);
                if (!value) {
                    return false;
                }
                if (endpoint.last_value
                    && *endpoint.last_value == value->value) {
                    continue;
                }
                endpoint.last_value = value->value;
                if (mark_dirty) {
                    dirty_outputs_.insert_or_assign(
                        endpoint.endpoint, std::move(*value));
                }
            }
            return true;
        }

        [[nodiscard]] SystemCKernelExecutionReceipt make_execution_receipt(
            const SystemCKernelExecutionCode code,
            const std::string_view detail_text,
            std::vector<SystemCKernelExecutionSample> samples = { },
            std::optional<SystemCKernelExecutionStatus> forced_status = std::nullopt,
            std::optional<detail::SystemCKernelNativeObservation> observation = std::nullopt)
        {
            if (!observation) {
                observation = last_observation_;
            }
            if (!observation) {
                observation = detail::SystemCKernelNativeObservation { };
            }
            SystemCKernelExecutionReceipt receipt;
            receipt.session_state = session_state_;
            receipt.status = forced_status.value_or(observation->status);
            receipt.code = code;
            receipt.published = published_;
            receipt.current_activity = observation->current_activity;
            receipt.future_activity = observation->future_activity;
            receipt.next_activity_time_fs = observation->next_activity_time_fs;
            auto region = SystemCAccelleraRegion::quiescent;
            if (receipt.status == SystemCKernelExecutionStatus::terminal
                || receipt.status == SystemCKernelExecutionStatus::error
                || receipt.status == SystemCKernelExecutionStatus::stopped) {
                region = SystemCAccelleraRegion::terminal;
            }
            receipt.order = make_execution_order(*observation, region);
            std::ranges::sort(samples, { },
                &SystemCKernelExecutionSample::order);
            receipt.samples = std::move(samples);
            receipt.detail.assign(detail_text.substr(
                0U, execution_limits_.max_detail_bytes));
            return receipt;
        }

        [[nodiscard]] SystemCKernelDirectResult respond_execution(
            const SystemCKernelDirectResultStatus status,
            const SystemCKernelExecutionCode code,
            const std::string_view detail_text,
            std::vector<SystemCKernelExecutionSample> samples = { },
            std::optional<SystemCKernelExecutionStatus> forced_status = std::nullopt,
            std::optional<detail::SystemCKernelNativeObservation> observation = std::nullopt)
        {
            auto receipt = make_execution_receipt(code, detail_text,
                std::move(samples), forced_status, observation);
            return { status, std::move(receipt) };
        }

        [[nodiscard]] SystemCKernelDirectResult reject_execution(
            const std::string_view detail_text,
            const SystemCKernelExecutionCode code = SystemCKernelExecutionCode::state)
        {
            return respond_execution(SystemCKernelDirectResultStatus::rejected,
                code, detail_text);
        }

        [[nodiscard]] SystemCKernelDirectResult apply_inputs(
            const SystemCKernelApplyInputsRequest& request)
        {
            if (!execution_available() || execution_stopped_) {
                return reject_execution(
                    "SystemC input application requires a live quiescent session");
            }
            diagnostic::Engine diagnostics;
            const auto& payload = request.payload;
            if (!valid_scalar(payload.value, execution_limits_, diagnostics)) {
                return reject_execution(
                    "invalid SystemC input-application payload",
                    SystemCKernelExecutionCode::payload);
            }
            const auto endpoint = endpoints_.find(request.endpoint);
            if (endpoint == endpoints_.end()
                || endpoint->second.object != request.object
                || (endpoint->second.scalar == nullptr
                    && endpoint->second.typed == nullptr)) {
                return reject_execution(
                    "SystemC input application targets an unknown or non-input endpoint");
            }
            std::string error;
            std::optional<detail::SystemCKernelNativeObservation> observation;
            {
                ContextActivation active { context_.get() };
                const auto applied = payload.value.typed
                    ? endpoint->second.typed != nullptr
                        && detail::systemc_kernel_apply_value(
                            *endpoint->second.typed,
                            *payload.value.typed, error)
                    : endpoint->second.scalar != nullptr
                        && detail::systemc_kernel_apply_scalar(
                            *endpoint->second.scalar, payload.value, error);
                if (!applied) {
                    if (error.empty()) {
                        error = "SystemC input representation does not match the bound endpoint adapter";
                    }
                    return reject_execution(error,
                        SystemCKernelExecutionCode::payload);
                }
                observation = detail::systemc_kernel_observe_native(
                    *context_, time_resolution_fs_, error);
            }
            if (!observation) {
                auto previous = last_observation_;
                fail_and_rollback();
                return respond_execution(
                    SystemCKernelDirectResultStatus::failed,
                    SystemCKernelExecutionCode::upstream,
                    error.empty() ? "cannot observe applied SystemC input" : error,
                    { }, SystemCKernelExecutionStatus::error, previous);
            }
            last_observation_ = observation;
            return respond_execution(SystemCKernelDirectResultStatus::ok,
                SystemCKernelExecutionCode::none,
                "SystemC scalar input is applied and awaits delta advancement",
                { }, std::nullopt, observation);
        }

        [[nodiscard]] SystemCKernelDirectResult advance(
            const SystemCKernelAdvanceRequest& request)
        {
            if (!execution_available() || execution_stopped_) {
                return reject_execution(
                    "SystemC advancement requires a live nonterminal session");
            }
            diagnostic::Engine diagnostics;
            const auto& payload = request.payload;
            if (!known_advance_kind(payload.kind)
                || (payload.kind == SystemCKernelAdvanceKind::delta
                        ? payload.duration_fs != 0U
                        : payload.duration_fs == 0U
                            || payload.duration_fs > execution_limits_.max_advance_fs
                            || payload.duration_fs % time_resolution_fs_ != 0U)) {
                return reject_execution(
                    "invalid SystemC advancement payload",
                    SystemCKernelExecutionCode::payload);
            }
            std::string error;
            std::optional<detail::SystemCKernelNativeObservation> observation;
            {
                ContextActivation active { context_.get() };
                observation = detail::systemc_kernel_advance_native(*context_,
                    payload, execution_limits_, time_resolution_fs_, error);
                if (observation
                    && !refresh_outputs(*observation, true, error)) {
                    observation.reset();
                }
            }
            if (!observation) {
                auto previous = last_observation_;
                fail_and_rollback();
                return respond_execution(
                    SystemCKernelDirectResultStatus::failed,
                    SystemCKernelExecutionCode::upstream,
                    error.empty() ? "upstream SystemC advancement failed" : error,
                    { }, SystemCKernelExecutionStatus::error, previous);
            }
            last_observation_ = observation;
            execution_stopped_ = observation->status
                == SystemCKernelExecutionStatus::stopped;
            return respond_execution(SystemCKernelDirectResultStatus::ok,
                SystemCKernelExecutionCode::none,
                execution_stopped_
                    ? "upstream SystemC execution stopped at a terminal safe point"
                    : "upstream SystemC execution reached a bounded safe point",
                { }, std::nullopt, observation);
        }

        [[nodiscard]] SystemCKernelDirectResult next_activity(
            const SystemCKernelNextActivityRequest&)
        {
            if (!execution_available()) {
                return reject_execution(
                    "SystemC next-activity query requires a live session");
            }
            std::string error;
            std::optional<detail::SystemCKernelNativeObservation> observation;
            {
                ContextActivation active { context_.get() };
                observation = detail::systemc_kernel_observe_native(
                    *context_, time_resolution_fs_, error);
            }
            if (!observation) {
                return reject_execution(
                    error.empty() ? "cannot observe next SystemC activity" : error,
                    SystemCKernelExecutionCode::upstream);
            }
            last_observation_ = observation;
            return respond_execution(SystemCKernelDirectResultStatus::ok,
                SystemCKernelExecutionCode::none,
                "SystemC next activity is reported at an exact safe point",
                { }, std::nullopt, observation);
        }

        [[nodiscard]] SystemCKernelDirectResult drain_outputs(
            const SystemCKernelDrainOutputsRequest& request)
        {
            if (!execution_available()) {
                return reject_execution(
                    "SystemC dirty-output drain requires a live session");
            }
            const auto endpoint = endpoints_.find(request.endpoint);
            const auto is_output = endpoint != endpoints_.end()
                && (endpoint->second.typed != nullptr
                        ? endpoint->second.typed->backend_direction()
                            == backend_endpoint_direction::output
                        : endpoint->second.scalar != nullptr
                            && endpoint->second.scalar->backend_direction()
                                == backend_endpoint_direction::output);
            if (endpoint == endpoints_.end()
                || endpoint->second.object != request.object
                || !is_output) {
                return reject_execution(
                    "SystemC dirty-output drain targets an unknown or non-output endpoint");
            }
            std::vector<SystemCKernelExecutionSample> samples;
            const auto dirty = dirty_outputs_.find(request.endpoint);
            if (dirty != dirty_outputs_.end()) {
                samples.push_back(dirty->second);
            }
            auto result = respond_execution(SystemCKernelDirectResultStatus::ok,
                SystemCKernelExecutionCode::none,
                samples.empty() ? "SystemC output has no undrained change"
                                : "SystemC dirty output is drained exactly once",
                samples);
            if (result.status == SystemCKernelDirectResultStatus::ok
                && !samples.empty()) {
                dirty_outputs_.erase(request.endpoint);
            }
            return result;
        }

        [[nodiscard]] SystemCKernelDirectResult report(
            const SystemCKernelReportRequest&)
        {
            if (!execution_available()) {
                return reject_execution(
                    "SystemC execution report requires a live session");
            }
            const auto text = std::string { "identity=" } + canonical_identity_
                + "; objects=" + std::to_string(roots_.size())
                + "; endpoints=" + std::to_string(endpoints_.size())
                + "; dirty=" + std::to_string(dirty_outputs_.size())
                + "; time_fs="
                + std::to_string(last_observation_
                        ? last_observation_->time_fs
                        : 0U)
                + "; delta="
                + std::to_string(last_observation_
                        ? last_observation_->delta
                        : 0U);
            return respond_execution(SystemCKernelDirectResultStatus::ok,
                SystemCKernelExecutionCode::none, text);
        }

        [[nodiscard]] SystemCKernelDirectResult inspect(
            const SystemCKernelInspectRequest& request)
        {
            if (!execution_available()) {
                return reject_execution(
                    "SystemC inspection requires a live session");
            }
            const auto endpoint = endpoints_.find(request.endpoint);
            if (endpoint == endpoints_.end()
                || endpoint->second.object != request.object
                || (endpoint->second.scalar == nullptr
                    && endpoint->second.typed == nullptr)) {
                return reject_execution(
                    "SystemC inspection targets an unknown value endpoint");
            }
            std::string error;
            std::optional<SystemCKernelExecutionSample> sample;
            {
                ContextActivation active { context_.get() };
                sample = sample_endpoint(endpoint->second, *last_observation_,
                    SystemCAccelleraRegion::quiescent,
                    dirty_outputs_.contains(request.endpoint), error);
            }
            if (!sample) {
                return reject_execution(error,
                    SystemCKernelExecutionCode::upstream);
            }
            return respond_execution(SystemCKernelDirectResultStatus::ok,
                SystemCKernelExecutionCode::none,
                "SystemC value endpoint is inspected at a safe point",
                { *sample });
        }

        [[nodiscard]] SystemCKernelDirectResult snapshot(
            const SystemCKernelSnapshotRequest&)
        {
            if (!execution_available()) {
                return reject_execution(
                    "SystemC snapshot requires a live session");
            }
            if (endpoints_.size() > execution_limits_.max_samples_per_message) {
                return reject_execution(
                    "SystemC snapshot exceeds its governed sample limit",
                    SystemCKernelExecutionCode::resource);
            }
            std::vector<SystemCKernelExecutionSample> samples;
            samples.reserve(endpoints_.size());
            std::string error;
            {
                ContextActivation active { context_.get() };
                for (const auto& [identity, endpoint] : endpoints_) {
                    if (endpoint.scalar == nullptr && endpoint.typed == nullptr) {
                        continue;
                    }
                    auto sample = sample_endpoint(endpoint, *last_observation_,
                        SystemCAccelleraRegion::quiescent,
                        dirty_outputs_.contains(identity), error);
                    if (!sample) {
                        return reject_execution(error,
                            SystemCKernelExecutionCode::upstream);
                    }
                    samples.push_back(*sample);
                }
            }
            return respond_execution(SystemCKernelDirectResultStatus::ok,
                SystemCKernelExecutionCode::none,
                "SystemC value snapshot is complete and deterministically ordered",
                std::move(samples));
        }

        [[nodiscard]] SystemCKernelDirectResult fail_mutation(
            const std::string_view detail,
            const SystemCKernelLifecycleCode code = SystemCKernelLifecycleCode::upstream)
        {
            fail_and_rollback();
            return respond(SystemCKernelDirectResultStatus::failed, detail, code);
        }

        void fail_and_rollback() noexcept
        {
            teardown_resources();
            session_state_ = SystemCKernelSessionState::failed;
            published_ = false;
        }

        void teardown(const bool terminal) noexcept
        {
            if (session_state_ == SystemCKernelSessionState::terminal) {
                return;
            }
            teardown_resources();
            if (terminal || session_state_ != SystemCKernelSessionState::vacant) {
                session_state_ = SystemCKernelSessionState::terminal;
            }
            published_ = false;
        }

        void teardown_resources() noexcept
        {
            if (context_ != nullptr) {
                try {
                    ContextActivation active { context_.get() };
                    try {
                        if (session_state_
                            == SystemCKernelSessionState::quiescent) {
                            context_->end();
                        }
                    } catch (...) {
                    }
                    registry_.reset();
                } catch (...) {
                    registry_.reset();
                }
                context_.reset();
                gLiveSystemCContexts.fetch_sub(1U, std::memory_order_relaxed);
            } else {
                registry_.reset();
            }
            roots_.clear();
            endpoints_.clear();
            dirty_outputs_.clear();
            last_observation_.reset();
            execution_stopped_ = false;
        }

        [[nodiscard]] SystemCKernelDirectResult respond(
            const SystemCKernelDirectResultStatus status,
            const std::string_view detail,
            const SystemCKernelLifecycleCode code = SystemCKernelLifecycleCode::none)
        {
            SystemCKernelLifecycleReceipt receipt;
            receipt.state = session_state_;
            receipt.published = published_;
            receipt.code = code;
            receipt.staged_objects = static_cast<std::uint32_t>(roots_.size());
            receipt.staged_bindings = static_cast<std::uint32_t>(endpoints_.size());
            receipt.published_objects = published_
                ? static_cast<std::uint32_t>(roots_.size())
                : 0U;
            receipt.context_generation = context_generation_;
            receipt.detail.assign(detail.substr(0U, session_limits_.max_detail_bytes));
            return { status, std::move(receipt) };
        }

        SystemCKernelProtocolLimits protocol_limits_;
        SystemCKernelSessionLimits session_limits_;
        SystemCKernelExecutionLimits execution_limits_;
        std::mutex mutex_;
        SystemCKernelSessionState session_state_ {
            SystemCKernelSessionState::vacant
        };
        SystemCIslandId island_;
        std::string canonical_identity_;
        std::uint64_t context_generation_ { };
        std::uint64_t time_resolution_fs_ { 1U };
        std::uint64_t next_result_sequence_ { 1U };
        bool published_ { };
        bool execution_stopped_ { };
        std::unique_ptr<sc_core::sc_simcontext> context_;
        std::unique_ptr<HierarchyRegistry> registry_;
        std::map<SystemCObjectId, RootRecord> roots_;
        std::map<SystemCEndpointId, EndpointRecord> endpoints_;
        std::map<SystemCEndpointId, SystemCKernelExecutionSample> dirty_outputs_;
        std::optional<detail::SystemCKernelNativeObservation> last_observation_;
    };

} // namespace

const char* systemc_kernel_lifecycle_diagnostic_code(
    const SystemCKernelLifecycleCode code) noexcept
{
    switch (code) {
    case SystemCKernelLifecycleCode::none:
        return "";
    case SystemCKernelLifecycleCode::session_state:
        return "FSIM-SC-S001";
    case SystemCKernelLifecycleCode::payload:
        return "FSIM-SC-S002";
    case SystemCKernelLifecycleCode::resource:
        return "FSIM-SC-S003";
    case SystemCKernelLifecycleCode::upstream:
        return "FSIM-SC-S004";
    }
    return "FSIM-SC-S002";
}

std::unique_ptr<SystemCKernelBackend>
make_systemc_kernel_session_backend(
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(session_limits, diagnostics)) {
        return nullptr;
    }
    if (protocol_limits.max_identity_bytes
        < session_limits.max_identity_bytes) {
        report_resource_error(diagnostics,
            "SystemC session identity limits exceed the identity limit");
        return nullptr;
    }
    return std::make_unique<SystemCKernelSessionBackend>(
        protocol_limits, session_limits);
}

std::unique_ptr<SystemCKernelBackend>
make_systemc_kernel_session_backend(
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(session_limits, diagnostics)
        || !detail::systemc_kernel_execution_limits_valid(
            execution_limits, diagnostics)) {
        return nullptr;
    }
    if (protocol_limits.max_identity_bytes
        < session_limits.max_identity_bytes) {
        report_resource_error(diagnostics,
            "SystemC execution session identity limits exceed the identity limit");
        return nullptr;
    }
    return std::make_unique<SystemCKernelSessionBackend>(
        protocol_limits, session_limits, execution_limits);
}

std::size_t systemc_kernel_backend_live_contexts() noexcept
{
    return gLiveSystemCContexts.load(std::memory_order_relaxed);
}

} // namespace fsim::systemc
