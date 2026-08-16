// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_session.hpp"

#include "fsim/systemc/accellera.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/systemc/kernel_backend_execution.hpp"
#include "context_activation.hpp"
#include "kernel_backend_execution_internal.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
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

    class ByteWriter final {
    public:
        void append_u8(const std::uint8_t value)
        {
            bytes_.push_back(static_cast<std::byte>(value));
        }

        void append_u16(const std::uint16_t value)
        {
            append_integral(value);
        }

        void append_u32(const std::uint32_t value)
        {
            append_integral(value);
        }

        void append_u64(const std::uint64_t value)
        {
            append_integral(value);
        }

        void append_i64(const std::int64_t value)
        {
            append_u64(static_cast<std::uint64_t>(value));
        }

        void append_text(const std::string_view value)
        {
            if (value.empty()) {
                return;
            }
            const auto* begin = reinterpret_cast<const std::byte*>(value.data());
            bytes_.insert(bytes_.end(), begin, begin + value.size());
        }

        [[nodiscard]] std::vector<std::byte> take() &&
        {
            return std::move(bytes_);
        }

    private:
        template <typename T>
            requires(std::is_unsigned_v<T>)
        void append_integral(const T value)
        {
            for (std::size_t index = 0U; index < sizeof(T); ++index) {
                const auto shifted = value >> (index * 8U);
                bytes_.push_back(
                    static_cast<std::byte>(shifted & static_cast<T>(0xffU)));
            }
        }

        std::vector<std::byte> bytes_;
    };

    class ByteReader final {
    public:
        explicit ByteReader(const std::span<const std::byte> bytes)
            : bytes_ { bytes }
        {
        }

        [[nodiscard]] std::optional<std::uint8_t> read_u8()
        {
            return read_integral<std::uint8_t>();
        }

        [[nodiscard]] std::optional<std::uint16_t> read_u16()
        {
            return read_integral<std::uint16_t>();
        }

        [[nodiscard]] std::optional<std::uint32_t> read_u32()
        {
            return read_integral<std::uint32_t>();
        }

        [[nodiscard]] std::optional<std::uint64_t> read_u64()
        {
            return read_integral<std::uint64_t>();
        }

        [[nodiscard]] std::optional<std::int64_t> read_i64()
        {
            const auto value = read_u64();
            if (!value) {
                return std::nullopt;
            }
            return static_cast<std::int64_t>(*value);
        }

        [[nodiscard]] std::optional<std::string> read_text(
            const std::size_t size)
        {
            if (size > bytes_.size() - offset_) {
                return std::nullopt;
            }
            const auto* begin = reinterpret_cast<const char*>(
                bytes_.data() + offset_);
            offset_ += size;
            return std::string { begin, size };
        }

        [[nodiscard]] bool done() const noexcept
        {
            return offset_ == bytes_.size();
        }

    private:
        template <typename T>
            requires(std::is_unsigned_v<T>)
        [[nodiscard]] std::optional<T> read_integral()
        {
            if (sizeof(T) > bytes_.size() - offset_) {
                return std::nullopt;
            }
            std::uint64_t value { };
            for (std::size_t index = 0U; index < sizeof(T); ++index) {
                value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(
                             bytes_[offset_ + index]))
                    << (index * 8U);
            }
            offset_ += sizeof(T);
            return static_cast<T>(value);
        }

        std::span<const std::byte> bytes_;
        std::size_t offset_ { };
    };

    std::optional<std::uint32_t> checked_size(const std::size_t size,
        diagnostic::Engine& diagnostics, const std::string_view role)
    {
        if (size > std::numeric_limits<std::uint32_t>::max()) {
            report_resource_error(diagnostics,
                std::string { "SystemC session " } + std::string { role }
                    + " exceeds the 32-bit payload boundary");
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(size);
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

    bool known_state(const SystemCKernelSessionState state) noexcept
    {
        switch (state) {
        case SystemCKernelSessionState::vacant:
        case SystemCKernelSessionState::constructing:
        case SystemCKernelSessionState::elaborated:
        case SystemCKernelSessionState::quiescent:
        case SystemCKernelSessionState::terminal:
        case SystemCKernelSessionState::failed:
            return true;
        }
        return false;
    }

    bool known_lifecycle_code(const SystemCKernelLifecycleCode code) noexcept
    {
        switch (code) {
        case SystemCKernelLifecycleCode::none:
        case SystemCKernelLifecycleCode::session_state:
        case SystemCKernelLifecycleCode::payload:
        case SystemCKernelLifecycleCode::resource:
        case SystemCKernelLifecycleCode::upstream:
            return true;
        }
        return false;
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

        [[nodiscard]] SystemCKernelTransportResult exchange(
            const std::span<const std::byte> request_bytes) noexcept override
        {
            std::lock_guard lock { mutex_ };
            try {
                diagnostic::Engine diagnostics;
                const auto request = deserialize_systemc_kernel_message(
                    request_bytes, protocol_limits_, diagnostics);
                if (!request
                    || request->header.direction
                        != SystemCKernelMessageDirection::request) {
                    return { SystemCKernelTransportStatus::rejected, { } };
                }
                return dispatch(*request);
            } catch (...) {
                fail_and_rollback();
                return { SystemCKernelTransportStatus::disconnected, { } };
            }
        }

        void close() noexcept override
        {
            std::lock_guard lock { mutex_ };
            teardown(false);
        }

    private:
        [[nodiscard]] SystemCKernelTransportResult dispatch(
            const SystemCKernelMessage& request)
        {
            if (request.header.operation == SystemCKernelOperation::handshake) {
                return respond(request, SystemCKernelMessageStatus::ok,
                    "Accellera SystemC 3.0.2 kernel session backend");
            }
            if (session_state_ != SystemCKernelSessionState::vacant
                && request.header.island != island_) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "request island does not own this backend session",
                    SystemCKernelLifecycleCode::session_state);
            }
            switch (request.header.operation) {
            case SystemCKernelOperation::create_session:
                return create_session(request);
            case SystemCKernelOperation::create_object:
                return create_object(request);
            case SystemCKernelOperation::bind_endpoint:
                return bind_endpoint(request);
            case SystemCKernelOperation::elaborate:
                return elaborate(request);
            case SystemCKernelOperation::start:
                return start(request);
            case SystemCKernelOperation::apply_inputs:
                return apply_inputs(request);
            case SystemCKernelOperation::advance:
                return advance(request);
            case SystemCKernelOperation::next_activity:
                return next_activity(request);
            case SystemCKernelOperation::drain_outputs:
                return drain_outputs(request);
            case SystemCKernelOperation::report:
                return report(request);
            case SystemCKernelOperation::inspect:
                return inspect(request);
            case SystemCKernelOperation::snapshot:
                return snapshot(request);
            case SystemCKernelOperation::teardown:
                teardown(true);
                return respond(request, SystemCKernelMessageStatus::ok,
                    "SystemC session reached terminal teardown");
            default:
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "operation is outside the session-lifecycle protocol slice",
                    SystemCKernelLifecycleCode::session_state);
            }
        }

        [[nodiscard]] SystemCKernelTransportResult create_session(
            const SystemCKernelMessage& request)
        {
            if (session_state_ != SystemCKernelSessionState::vacant) {
                fail_and_rollback();
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "SystemC session construction cannot be repeated",
                    SystemCKernelLifecycleCode::session_state);
            }
            diagnostic::Engine diagnostics;
            const auto payload = deserialize_systemc_create_session_payload(
                request.payload, session_limits_, diagnostics);
            if (!payload) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "invalid SystemC session-construction payload",
                    SystemCKernelLifecycleCode::payload);
            }
            auto expected_island = make_systemc_island_id(
                payload->canonical_identity, protocol_limits_, diagnostics);
            if (!expected_island || *expected_island != request.header.island) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "SystemC session identity does not match its island ID",
                    SystemCKernelLifecycleCode::session_state);
            }

            std::unique_ptr<sc_core::sc_simcontext> candidate;
            try {
                candidate = std::make_unique<sc_core::sc_simcontext>();
            } catch (...) {
                return respond(request, SystemCKernelMessageStatus::failed,
                    "cannot allocate the upstream SystemC context",
                    SystemCKernelLifecycleCode::upstream);
            }
            std::string error;
            std::unique_ptr<HierarchyRegistry> candidate_registry;
            try {
                ContextActivation active { candidate.get() };
                sc_core::sc_set_time_resolution(
                    static_cast<double>(payload->time_resolution_fs),
                    sc_core::SC_FS);
                candidate_registry = HierarchyRegistry::load(
                    std::filesystem::path { payload->plugin_path }, error);
            } catch (const std::exception& exception) {
                error = exception.what();
            } catch (...) {
                error = "unknown upstream context-construction failure";
            }
            if (!candidate_registry) {
                candidate.reset();
                return respond(request, SystemCKernelMessageStatus::failed,
                    error.empty() ? "cannot load the SystemC session plug-in"
                                  : error,
                    SystemCKernelLifecycleCode::upstream);
            }

            island_ = request.header.island;
            canonical_identity_ = payload->canonical_identity;
            time_resolution_fs_ = payload->time_resolution_fs;
            context_generation_ = gNextContextGeneration.fetch_add(1U, std::memory_order_relaxed);
            context_ = std::move(candidate);
            registry_ = std::move(candidate_registry);
            session_state_ = SystemCKernelSessionState::constructing;
            gLiveSystemCContexts.fetch_add(1U, std::memory_order_relaxed);
            return respond(request, SystemCKernelMessageStatus::ok,
                "SystemC session owns one fresh upstream context");
        }

        [[nodiscard]] SystemCKernelTransportResult create_object(
            const SystemCKernelMessage& request)
        {
            if (session_state_ != SystemCKernelSessionState::constructing
                || context_ == nullptr || registry_ == nullptr) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "SystemC objects can be created only during construction",
                    SystemCKernelLifecycleCode::session_state);
            }
            diagnostic::Engine diagnostics;
            const auto payload = deserialize_systemc_create_object_payload(
                request.payload, session_limits_, diagnostics);
            if (!payload) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "invalid SystemC object-construction payload",
                    SystemCKernelLifecycleCode::payload);
            }
            if (roots_.size() >= session_limits_.max_objects) {
                return fail_mutation(request,
                    "SystemC session object limit is exhausted",
                    SystemCKernelLifecycleCode::resource);
            }
            const auto hierarchy = make_systemc_hierarchy_id(island_,
                payload->hierarchy_path, protocol_limits_, diagnostics);
            const auto object = hierarchy
                ? make_systemc_object_id(*hierarchy, payload->object_path,
                      protocol_limits_, diagnostics)
                : std::nullopt;
            if (!hierarchy || !object || *hierarchy != request.header.hierarchy
                || *object != request.header.object) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "SystemC object paths do not match their typed identities",
                    SystemCKernelLifecycleCode::session_state);
            }
            if (roots_.contains(*object)
                || std::ranges::any_of(roots_, [&](const auto& entry) {
                       return entry.second.object_path == payload->object_path;
                   })) {
                return fail_mutation(request,
                    "SystemC object construction cannot be repeated",
                    SystemCKernelLifecycleCode::session_state);
            }

            std::vector<std::pair<std::string, std::int64_t>> parameters;
            parameters.reserve(payload->parameters.size());
            for (const auto& parameter : payload->parameters) {
                parameters.emplace_back(parameter.name, parameter.value);
            }
            std::string error;
            std::optional<ModuleDescription> description;
            try {
                ContextActivation active { context_.get() };
                description = registry_->instantiate(payload->factory,
                    payload->instance, 0U, parameters, error);
                const auto* native = sc_core::sc_find_object(
                    payload->object_path.c_str());
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
                return fail_mutation(request,
                    error.empty() ? "native SystemC factory rejected construction"
                                  : error);
            }
            roots_.emplace(*object,
                RootRecord { *hierarchy, *object, payload->object_path,
                    description->handle });
            return respond(request, SystemCKernelMessageStatus::ok,
                "SystemC object is staged in the owned upstream context");
        }

        [[nodiscard]] SystemCKernelTransportResult bind_endpoint(
            const SystemCKernelMessage& request)
        {
            if (session_state_ != SystemCKernelSessionState::constructing
                || context_ == nullptr) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "SystemC endpoints can be bound only during construction",
                    SystemCKernelLifecycleCode::session_state);
            }
            diagnostic::Engine diagnostics;
            const auto payload = deserialize_systemc_bind_endpoint_payload(
                request.payload, session_limits_, diagnostics);
            if (!payload) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "invalid SystemC endpoint-binding payload",
                    SystemCKernelLifecycleCode::payload);
            }
            const auto root = roots_.find(request.header.object);
            if (root == roots_.end()
                || root->second.hierarchy != request.header.hierarchy) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "SystemC endpoint references an unknown staged object",
                    SystemCKernelLifecycleCode::session_state);
            }
            const auto endpoint = make_systemc_endpoint_id(root->second.object,
                payload->endpoint_path, protocol_limits_, diagnostics);
            if (!endpoint || *endpoint != request.header.endpoint) {
                return respond(request, SystemCKernelMessageStatus::rejected,
                    "SystemC endpoint path does not match its typed identity",
                    SystemCKernelLifecycleCode::session_state);
            }
            if (endpoints_.contains(*endpoint)
                || endpoints_.size() >= session_limits_.max_bindings) {
                return fail_mutation(request,
                    "SystemC endpoint binding is repeated or exceeds its limit",
                    endpoints_.contains(*endpoint)
                        ? SystemCKernelLifecycleCode::session_state
                        : SystemCKernelLifecycleCode::resource);
            }
            if (!payload->endpoint_path.starts_with(
                    root->second.object_path + ".")) {
                return fail_mutation(request,
                    "SystemC endpoint is outside its owning root hierarchy",
                    SystemCKernelLifecycleCode::session_state);
            }

            std::string error;
            backend_scalar_endpoint* scalar { };
            backend_value_endpoint* typed { };
            try {
                ContextActivation active { context_.get() };
                auto* endpoint_object = sc_core::sc_find_object(
                    payload->endpoint_path.c_str());
                auto* interface_object = sc_core::sc_find_object(
                    payload->interface_path.c_str());
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
                return fail_mutation(request, error);
            }
            endpoints_.emplace(*endpoint,
                EndpointRecord { *endpoint, root->second.object,
                    payload->endpoint_path, payload->interface_path, scalar,
                    typed, { } });
            return respond(request, SystemCKernelMessageStatus::ok,
                "SystemC endpoint binding is staged and identity-checked");
        }

        [[nodiscard]] SystemCKernelTransportResult elaborate(
            const SystemCKernelMessage& request)
        {
            if (session_state_ != SystemCKernelSessionState::constructing
                || context_ == nullptr || roots_.empty()) {
                return respond(request, SystemCKernelMessageStatus::rejected,
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
                return fail_mutation(request, error);
            }
            session_state_ = SystemCKernelSessionState::elaborated;
            return respond(request, SystemCKernelMessageStatus::ok,
                "upstream SystemC elaboration completed without publication");
        }

        [[nodiscard]] SystemCKernelTransportResult start(
            const SystemCKernelMessage& request)
        {
            if (session_state_ != SystemCKernelSessionState::elaborated
                || context_ == nullptr) {
                return respond(request, SystemCKernelMessageStatus::rejected,
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
                return fail_mutation(request, error);
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
                return fail_mutation(request,
                    error.empty() ? "cannot establish initial execution state"
                                  : error);
            }
            return respond(request, SystemCKernelMessageStatus::ok,
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

        [[nodiscard]] SystemCKernelTransportResult respond_execution(
            const SystemCKernelMessage& request,
            const SystemCKernelMessageStatus message_status,
            const SystemCKernelExecutionCode code,
            const std::string_view detail_text,
            std::vector<SystemCKernelExecutionSample> samples = { },
            std::optional<SystemCKernelExecutionStatus> forced_status = std::nullopt,
            std::optional<detail::SystemCKernelNativeObservation> observation = std::nullopt)
        {
            auto receipt = make_execution_receipt(code, detail_text,
                std::move(samples), forced_status, observation);
            diagnostic::Engine diagnostics;
            auto payload = serialize_systemc_execution_receipt(
                receipt, execution_limits_, diagnostics);
            if (!payload) {
                return { SystemCKernelTransportStatus::disconnected, { } };
            }
            SystemCKernelMessage response;
            response.header = request.header;
            response.header.direction = SystemCKernelMessageDirection::response;
            response.header.status = message_status;
            response.header.correlation = request.header.sequence;
            response.payload = std::move(*payload);
            auto bytes = serialize_systemc_kernel_message(
                response, protocol_limits_, diagnostics);
            if (!bytes) {
                return { SystemCKernelTransportStatus::disconnected, { } };
            }
            return { SystemCKernelTransportStatus::ok, std::move(*bytes) };
        }

        [[nodiscard]] SystemCKernelTransportResult reject_execution(
            const SystemCKernelMessage& request, const std::string_view detail_text,
            const SystemCKernelExecutionCode code = SystemCKernelExecutionCode::state)
        {
            return respond_execution(request, SystemCKernelMessageStatus::rejected,
                code, detail_text);
        }

        [[nodiscard]] SystemCKernelTransportResult apply_inputs(
            const SystemCKernelMessage& request)
        {
            if (!execution_available() || execution_stopped_) {
                return reject_execution(request,
                    "SystemC input application requires a live quiescent session");
            }
            diagnostic::Engine diagnostics;
            const auto payload = deserialize_systemc_apply_inputs_payload(
                request.payload, execution_limits_, diagnostics);
            if (!payload) {
                return reject_execution(request,
                    "invalid SystemC input-application payload",
                    SystemCKernelExecutionCode::payload);
            }
            const auto endpoint = endpoints_.find(request.header.endpoint);
            if (endpoint == endpoints_.end()
                || endpoint->second.object != request.header.object
                || (endpoint->second.scalar == nullptr
                    && endpoint->second.typed == nullptr)) {
                return reject_execution(request,
                    "SystemC input application targets an unknown or non-input endpoint");
            }
            std::string error;
            std::optional<detail::SystemCKernelNativeObservation> observation;
            {
                ContextActivation active { context_.get() };
                const auto applied = payload->value.typed
                    ? endpoint->second.typed != nullptr
                        && detail::systemc_kernel_apply_value(
                            *endpoint->second.typed,
                            *payload->value.typed, error)
                    : endpoint->second.scalar != nullptr
                        && detail::systemc_kernel_apply_scalar(
                            *endpoint->second.scalar, payload->value, error);
                if (!applied) {
                    if (error.empty()) {
                        error = "SystemC input representation does not match the bound endpoint adapter";
                    }
                    return reject_execution(request, error,
                        SystemCKernelExecutionCode::payload);
                }
                observation = detail::systemc_kernel_observe_native(
                    *context_, time_resolution_fs_, error);
            }
            if (!observation) {
                auto previous = last_observation_;
                fail_and_rollback();
                return respond_execution(request,
                    SystemCKernelMessageStatus::failed,
                    SystemCKernelExecutionCode::upstream,
                    error.empty() ? "cannot observe applied SystemC input" : error,
                    { }, SystemCKernelExecutionStatus::error, previous);
            }
            last_observation_ = observation;
            return respond_execution(request, SystemCKernelMessageStatus::ok,
                SystemCKernelExecutionCode::none,
                "SystemC scalar input is applied and awaits delta advancement",
                { }, std::nullopt, observation);
        }

        [[nodiscard]] SystemCKernelTransportResult advance(
            const SystemCKernelMessage& request)
        {
            if (!execution_available() || execution_stopped_) {
                return reject_execution(request,
                    "SystemC advancement requires a live nonterminal session");
            }
            diagnostic::Engine diagnostics;
            const auto payload = deserialize_systemc_advance_payload(
                request.payload, execution_limits_, diagnostics);
            if (!payload) {
                return reject_execution(request,
                    "invalid SystemC advancement payload",
                    SystemCKernelExecutionCode::payload);
            }
            std::string error;
            std::optional<detail::SystemCKernelNativeObservation> observation;
            {
                ContextActivation active { context_.get() };
                observation = detail::systemc_kernel_advance_native(*context_,
                    *payload, execution_limits_, time_resolution_fs_, error);
                if (observation
                    && !refresh_outputs(*observation, true, error)) {
                    observation.reset();
                }
            }
            if (!observation) {
                auto previous = last_observation_;
                fail_and_rollback();
                return respond_execution(request,
                    SystemCKernelMessageStatus::failed,
                    SystemCKernelExecutionCode::upstream,
                    error.empty() ? "upstream SystemC advancement failed" : error,
                    { }, SystemCKernelExecutionStatus::error, previous);
            }
            last_observation_ = observation;
            execution_stopped_ = observation->status
                == SystemCKernelExecutionStatus::stopped;
            return respond_execution(request, SystemCKernelMessageStatus::ok,
                SystemCKernelExecutionCode::none,
                execution_stopped_
                    ? "upstream SystemC execution stopped at a terminal safe point"
                    : "upstream SystemC execution reached a bounded safe point",
                { }, std::nullopt, observation);
        }

        [[nodiscard]] SystemCKernelTransportResult next_activity(
            const SystemCKernelMessage& request)
        {
            if (!execution_available() || !request.payload.empty()) {
                return reject_execution(request,
                    "SystemC next-activity query requires a live session and empty payload");
            }
            std::string error;
            std::optional<detail::SystemCKernelNativeObservation> observation;
            {
                ContextActivation active { context_.get() };
                observation = detail::systemc_kernel_observe_native(
                    *context_, time_resolution_fs_, error);
            }
            if (!observation) {
                return reject_execution(request,
                    error.empty() ? "cannot observe next SystemC activity" : error,
                    SystemCKernelExecutionCode::upstream);
            }
            last_observation_ = observation;
            return respond_execution(request, SystemCKernelMessageStatus::ok,
                SystemCKernelExecutionCode::none,
                "SystemC next activity is reported at an exact safe point",
                { }, std::nullopt, observation);
        }

        [[nodiscard]] SystemCKernelTransportResult drain_outputs(
            const SystemCKernelMessage& request)
        {
            if (!execution_available() || !request.payload.empty()) {
                return reject_execution(request,
                    "SystemC dirty-output drain requires a live session and empty payload");
            }
            const auto endpoint = endpoints_.find(request.header.endpoint);
            const auto is_output = endpoint != endpoints_.end()
                && (endpoint->second.typed != nullptr
                        ? endpoint->second.typed->backend_direction()
                            == backend_endpoint_direction::output
                        : endpoint->second.scalar != nullptr
                            && endpoint->second.scalar->backend_direction()
                                == backend_endpoint_direction::output);
            if (endpoint == endpoints_.end()
                || endpoint->second.object != request.header.object
                || !is_output) {
                return reject_execution(request,
                    "SystemC dirty-output drain targets an unknown or non-output endpoint");
            }
            std::vector<SystemCKernelExecutionSample> samples;
            const auto dirty = dirty_outputs_.find(request.header.endpoint);
            if (dirty != dirty_outputs_.end()) {
                samples.push_back(dirty->second);
            }
            auto result = respond_execution(request, SystemCKernelMessageStatus::ok,
                SystemCKernelExecutionCode::none,
                samples.empty() ? "SystemC output has no undrained change"
                                : "SystemC dirty output is drained exactly once",
                samples);
            if (result.status == SystemCKernelTransportStatus::ok
                && !samples.empty()) {
                dirty_outputs_.erase(request.header.endpoint);
            }
            return result;
        }

        [[nodiscard]] SystemCKernelTransportResult report(
            const SystemCKernelMessage& request)
        {
            if (!execution_available() || !request.payload.empty()) {
                return reject_execution(request,
                    "SystemC execution report requires a live session and empty payload");
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
            return respond_execution(request, SystemCKernelMessageStatus::ok,
                SystemCKernelExecutionCode::none, text);
        }

        [[nodiscard]] SystemCKernelTransportResult inspect(
            const SystemCKernelMessage& request)
        {
            if (!execution_available() || !request.payload.empty()) {
                return reject_execution(request,
                    "SystemC inspection requires a live session and empty payload");
            }
            const auto endpoint = endpoints_.find(request.header.endpoint);
            if (endpoint == endpoints_.end()
                || endpoint->second.object != request.header.object
                || (endpoint->second.scalar == nullptr
                    && endpoint->second.typed == nullptr)) {
                return reject_execution(request,
                    "SystemC inspection targets an unknown value endpoint");
            }
            std::string error;
            std::optional<SystemCKernelExecutionSample> sample;
            {
                ContextActivation active { context_.get() };
                sample = sample_endpoint(endpoint->second, *last_observation_,
                    SystemCAccelleraRegion::quiescent,
                    dirty_outputs_.contains(request.header.endpoint), error);
            }
            if (!sample) {
                return reject_execution(request, error,
                    SystemCKernelExecutionCode::upstream);
            }
            return respond_execution(request, SystemCKernelMessageStatus::ok,
                SystemCKernelExecutionCode::none,
                "SystemC value endpoint is inspected at a safe point",
                { *sample });
        }

        [[nodiscard]] SystemCKernelTransportResult snapshot(
            const SystemCKernelMessage& request)
        {
            if (!execution_available() || !request.payload.empty()) {
                return reject_execution(request,
                    "SystemC snapshot requires a live session and empty payload");
            }
            if (endpoints_.size() > execution_limits_.max_samples_per_message) {
                return reject_execution(request,
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
                        return reject_execution(request, error,
                            SystemCKernelExecutionCode::upstream);
                    }
                    samples.push_back(*sample);
                }
            }
            return respond_execution(request, SystemCKernelMessageStatus::ok,
                SystemCKernelExecutionCode::none,
                "SystemC value snapshot is complete and deterministically ordered",
                std::move(samples));
        }

        [[nodiscard]] SystemCKernelTransportResult fail_mutation(
            const SystemCKernelMessage& request, const std::string_view detail,
            const SystemCKernelLifecycleCode code = SystemCKernelLifecycleCode::upstream)
        {
            fail_and_rollback();
            return respond(
                request, SystemCKernelMessageStatus::failed, detail, code);
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

        [[nodiscard]] SystemCKernelTransportResult respond(
            const SystemCKernelMessage& request,
            const SystemCKernelMessageStatus status,
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

            diagnostic::Engine diagnostics;
            auto payload = serialize_systemc_lifecycle_receipt(
                receipt, session_limits_, diagnostics);
            if (!payload) {
                return { SystemCKernelTransportStatus::disconnected, { } };
            }
            SystemCKernelMessage response;
            response.header = request.header;
            response.header.direction = SystemCKernelMessageDirection::response;
            response.header.status = status;
            response.header.correlation = request.header.sequence;
            response.payload = std::move(*payload);
            auto bytes = serialize_systemc_kernel_message(
                response, protocol_limits_, diagnostics);
            if (!bytes) {
                return { SystemCKernelTransportStatus::disconnected, { } };
            }
            return { SystemCKernelTransportStatus::ok, std::move(*bytes) };
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

std::optional<std::vector<std::byte>>
serialize_systemc_create_session_payload(
    const SystemCKernelCreateSessionPayload& payload,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_create_session_payload(payload, limits, diagnostics)) {
        return std::nullopt;
    }
    const auto identity_size = checked_size(
        payload.canonical_identity.size(), diagnostics, "identity");
    const auto path_size = checked_size(
        payload.plugin_path.size(), diagnostics, "plug-in path");
    if (!identity_size || !path_size) {
        return std::nullopt;
    }
    ByteWriter writer;
    writer.append_u32(kSystemCKernelSessionPayloadVersion);
    writer.append_u32(*identity_size);
    writer.append_u32(*path_size);
    writer.append_u32(0U);
    writer.append_u64(payload.time_resolution_fs);
    writer.append_text(payload.canonical_identity);
    writer.append_text(payload.plugin_path);
    return std::move(writer).take();
}

std::optional<SystemCKernelCreateSessionPayload>
deserialize_systemc_create_session_payload(
    const std::span<const std::byte> bytes,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    ByteReader reader { bytes };
    const auto schema = reader.read_u32();
    const auto identity_size = reader.read_u32();
    const auto path_size = reader.read_u32();
    const auto reserved = reader.read_u32();
    const auto resolution = reader.read_u64();
    if (!schema || !identity_size || !path_size || !reserved || !resolution
        || *schema != kSystemCKernelSessionPayloadVersion || *reserved != 0U
        || *identity_size > limits.max_identity_bytes
        || *path_size > limits.max_plugin_path_bytes) {
        report_payload_error(diagnostics,
            "invalid SystemC session payload header or resource length");
        return std::nullopt;
    }
    auto identity = reader.read_text(*identity_size);
    auto path = reader.read_text(*path_size);
    if (!identity || !path || !reader.done()) {
        report_payload_error(diagnostics,
            "truncated or trailing SystemC session payload bytes");
        return std::nullopt;
    }
    SystemCKernelCreateSessionPayload result {
        std::move(*identity), std::move(*path), *resolution
    };
    if (!valid_create_session_payload(result, limits, diagnostics)) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::vector<std::byte>>
serialize_systemc_create_object_payload(
    const SystemCKernelCreateObjectPayload& payload,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_create_object_payload(payload, limits, diagnostics)) {
        return std::nullopt;
    }
    const auto hierarchy_size = checked_size(
        payload.hierarchy_path.size(), diagnostics, "hierarchy path");
    const auto object_size = checked_size(
        payload.object_path.size(), diagnostics, "object path");
    const auto factory_size = checked_size(
        payload.factory.size(), diagnostics, "factory name");
    const auto instance_size = checked_size(
        payload.instance.size(), diagnostics, "instance name");
    const auto parameter_count = checked_size(
        payload.parameters.size(), diagnostics, "parameter count");
    if (!hierarchy_size || !object_size || !factory_size || !instance_size
        || !parameter_count) {
        return std::nullopt;
    }
    ByteWriter writer;
    writer.append_u32(kSystemCKernelSessionPayloadVersion);
    writer.append_u32(*parameter_count);
    writer.append_u32(*hierarchy_size);
    writer.append_u32(*object_size);
    writer.append_u32(*factory_size);
    writer.append_u32(*instance_size);
    writer.append_text(payload.hierarchy_path);
    writer.append_text(payload.object_path);
    writer.append_text(payload.factory);
    writer.append_text(payload.instance);
    for (const auto& parameter : payload.parameters) {
        const auto name_size = checked_size(
            parameter.name.size(), diagnostics, "parameter name");
        if (!name_size) {
            return std::nullopt;
        }
        writer.append_u32(*name_size);
        writer.append_u32(0U);
        writer.append_i64(parameter.value);
        writer.append_text(parameter.name);
    }
    return std::move(writer).take();
}

std::optional<SystemCKernelCreateObjectPayload>
deserialize_systemc_create_object_payload(
    const std::span<const std::byte> bytes,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    ByteReader reader { bytes };
    const auto schema = reader.read_u32();
    const auto parameter_count = reader.read_u32();
    const auto hierarchy_size = reader.read_u32();
    const auto object_size = reader.read_u32();
    const auto factory_size = reader.read_u32();
    const auto instance_size = reader.read_u32();
    if (!schema || !parameter_count || !hierarchy_size || !object_size
        || !factory_size || !instance_size
        || *schema != kSystemCKernelSessionPayloadVersion
        || *parameter_count > limits.max_parameters_per_object
        || *hierarchy_size > limits.max_name_bytes
        || *object_size > limits.max_name_bytes
        || *factory_size > limits.max_name_bytes
        || *instance_size > limits.max_name_bytes) {
        report_payload_error(diagnostics,
            "invalid SystemC object payload header or resource length");
        return std::nullopt;
    }
    SystemCKernelCreateObjectPayload result;
    auto hierarchy = reader.read_text(*hierarchy_size);
    auto object = reader.read_text(*object_size);
    auto factory = reader.read_text(*factory_size);
    auto instance = reader.read_text(*instance_size);
    if (!hierarchy || !object || !factory || !instance) {
        report_payload_error(
            diagnostics, "truncated SystemC object identity payload");
        return std::nullopt;
    }
    result.hierarchy_path = std::move(*hierarchy);
    result.object_path = std::move(*object);
    result.factory = std::move(*factory);
    result.instance = std::move(*instance);
    result.parameters.reserve(*parameter_count);
    for (std::uint32_t index = 0U; index < *parameter_count; ++index) {
        const auto name_size = reader.read_u32();
        const auto reserved = reader.read_u32();
        const auto value = reader.read_i64();
        if (!name_size || !reserved || !value || *reserved != 0U
            || *name_size > limits.max_name_bytes) {
            report_payload_error(diagnostics,
                "invalid SystemC construction-parameter payload");
            return std::nullopt;
        }
        auto name = reader.read_text(*name_size);
        if (!name) {
            report_payload_error(diagnostics,
                "truncated SystemC construction-parameter name");
            return std::nullopt;
        }
        result.parameters.push_back({ std::move(*name), *value });
    }
    if (!reader.done()
        || !valid_create_object_payload(result, limits, diagnostics)) {
        if (!reader.done()) {
            report_payload_error(
                diagnostics, "trailing SystemC object payload bytes");
        }
        return std::nullopt;
    }
    return result;
}

std::optional<std::vector<std::byte>>
serialize_systemc_bind_endpoint_payload(
    const SystemCKernelBindEndpointPayload& payload,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_bind_payload(payload, limits, diagnostics)) {
        return std::nullopt;
    }
    const auto endpoint_size = checked_size(
        payload.endpoint_path.size(), diagnostics, "endpoint path");
    const auto interface_size = checked_size(
        payload.interface_path.size(), diagnostics, "interface path");
    if (!endpoint_size || !interface_size) {
        return std::nullopt;
    }
    ByteWriter writer;
    writer.append_u32(kSystemCKernelSessionPayloadVersion);
    writer.append_u32(*endpoint_size);
    writer.append_u32(*interface_size);
    writer.append_u32(0U);
    writer.append_text(payload.endpoint_path);
    writer.append_text(payload.interface_path);
    return std::move(writer).take();
}

std::optional<SystemCKernelBindEndpointPayload>
deserialize_systemc_bind_endpoint_payload(
    const std::span<const std::byte> bytes,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    ByteReader reader { bytes };
    const auto schema = reader.read_u32();
    const auto endpoint_size = reader.read_u32();
    const auto interface_size = reader.read_u32();
    const auto reserved = reader.read_u32();
    if (!schema || !endpoint_size || !interface_size || !reserved
        || *schema != kSystemCKernelSessionPayloadVersion || *reserved != 0U
        || *endpoint_size > limits.max_name_bytes
        || *interface_size > limits.max_name_bytes) {
        report_payload_error(diagnostics,
            "invalid SystemC binding payload header or resource length");
        return std::nullopt;
    }
    auto endpoint = reader.read_text(*endpoint_size);
    auto interface = reader.read_text(*interface_size);
    if (!endpoint || !interface || !reader.done()) {
        report_payload_error(diagnostics,
            "truncated or trailing SystemC binding payload bytes");
        return std::nullopt;
    }
    SystemCKernelBindEndpointPayload result {
        std::move(*endpoint), std::move(*interface)
    };
    if (!valid_bind_payload(result, limits, diagnostics)) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::vector<std::byte>>
serialize_systemc_lifecycle_receipt(
    const SystemCKernelLifecycleReceipt& receipt,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics) || !known_state(receipt.state)
        || !known_lifecycle_code(receipt.code)
        || receipt.detail.size() > limits.max_detail_bytes
        || receipt.detail.find('\0') != std::string::npos
        || (receipt.published
            && receipt.published_objects != receipt.staged_objects)
        || (!receipt.published && receipt.published_objects != 0U)) {
        report_payload_error(
            diagnostics, "invalid SystemC lifecycle receipt state or counts");
        return std::nullopt;
    }
    const auto detail_size = checked_size(
        receipt.detail.size(), diagnostics, "receipt detail");
    if (!detail_size) {
        return std::nullopt;
    }
    ByteWriter writer;
    writer.append_u32(kSystemCKernelSessionPayloadVersion);
    writer.append_u8(static_cast<std::uint8_t>(receipt.state));
    writer.append_u8(receipt.published ? 1U : 0U);
    writer.append_u16(static_cast<std::uint16_t>(receipt.code));
    writer.append_u32(receipt.staged_objects);
    writer.append_u32(receipt.staged_bindings);
    writer.append_u32(receipt.published_objects);
    writer.append_u32(*detail_size);
    writer.append_u64(receipt.context_generation);
    writer.append_text(receipt.detail);
    return std::move(writer).take();
}

std::optional<SystemCKernelLifecycleReceipt>
deserialize_systemc_lifecycle_receipt(
    const std::span<const std::byte> bytes,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    ByteReader reader { bytes };
    const auto schema = reader.read_u32();
    const auto state = reader.read_u8();
    const auto published = reader.read_u8();
    const auto code = reader.read_u16();
    const auto objects = reader.read_u32();
    const auto bindings = reader.read_u32();
    const auto published_objects = reader.read_u32();
    const auto detail_size = reader.read_u32();
    const auto generation = reader.read_u64();
    if (!schema || !state || !published || !code || !objects || !bindings
        || !published_objects || !detail_size || !generation
        || *schema != kSystemCKernelSessionPayloadVersion
        || *published > 1U || *detail_size > limits.max_detail_bytes) {
        report_payload_error(diagnostics,
            "invalid SystemC lifecycle receipt header or resource length");
        return std::nullopt;
    }
    auto detail = reader.read_text(*detail_size);
    if (!detail || !reader.done()) {
        report_payload_error(diagnostics,
            "truncated or trailing SystemC lifecycle receipt bytes");
        return std::nullopt;
    }
    SystemCKernelLifecycleReceipt result {
        static_cast<SystemCKernelSessionState>(*state), *published != 0U,
        static_cast<SystemCKernelLifecycleCode>(*code), *objects, *bindings,
        *published_objects, *generation,
        std::move(*detail)
    };
    if (!known_state(result.state) || !known_lifecycle_code(result.code)
        || (result.published
            && result.published_objects != result.staged_objects)
        || (!result.published && result.published_objects != 0U)) {
        report_payload_error(
            diagnostics, "inconsistent SystemC lifecycle receipt state");
        return std::nullopt;
    }
    return result;
}

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
            < session_limits.max_identity_bytes
        || protocol_limits.max_payload_bytes < session_limits.max_detail_bytes
        || protocol_limits.max_message_bytes
            < kSystemCKernelMessageHeaderBytes
                + session_limits.max_detail_bytes) {
        report_resource_error(diagnostics,
            "SystemC session limits exceed the enclosing protocol limits");
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
    constexpr std::size_t receipt_header_bytes = 76U;
    constexpr std::size_t sample_bytes = 80U;
    if (execution_limits.max_samples_per_message
        > (std::numeric_limits<std::size_t>::max()
              - receipt_header_bytes - execution_limits.max_detail_bytes)
            / sample_bytes) {
        report_resource_error(diagnostics,
            "SystemC execution receipt limits overflow their encoded size");
        return nullptr;
    }
    const auto maximum_payload = receipt_header_bytes
        + sample_bytes * execution_limits.max_samples_per_message
        + execution_limits.max_detail_bytes;
    if (protocol_limits.max_identity_bytes
            < session_limits.max_identity_bytes
        || protocol_limits.max_payload_bytes < maximum_payload
        || protocol_limits.max_message_bytes
            < kSystemCKernelMessageHeaderBytes + maximum_payload) {
        report_resource_error(diagnostics,
            "SystemC execution limits exceed the enclosing protocol limits");
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
