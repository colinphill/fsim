// SPDX-License-Identifier: Apache-2.0
#include "application_systemc_trace.hpp"

#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace fsim::app::application_detail {
namespace {

    struct PendingBatch {
        runtime::SimulationTick time { };
        std::uint64_t delta { };
        runtime::TraceRegion region { runtime::TraceRegion::Postponed };
        std::string identity;
        std::vector<TraceObservationValue> values;
    };

    [[nodiscard]] bool coordinate_precedes(
        const runtime::SimulationTick left_time,
        const std::uint64_t left_delta,
        const runtime::SimulationTick right_time,
        const std::uint64_t right_delta) noexcept
    {
        return left_time < right_time
            || (left_time == right_time && left_delta < right_delta);
    }

    [[nodiscard]] runtime::PackedLogic4 trace_value_unchecked(
        const systemc::SystemCKernelValue& value)
    {
        if (value.kind == systemc::SystemCKernelValueKind::bit2) {
            std::vector<std::uint64_t> zeros(value.planes.front().size());
            return runtime::PackedLogic4::from_word_planes(
                value.width, value.planes[0], zeros);
        }
        if (value.kind == systemc::SystemCKernelValueKind::logic4) {
            return runtime::PackedLogic4::from_word_planes(
                value.width, value.planes[0], value.planes[1]);
        }
        runtime::PackedLogic4 result(value.width);
        for (std::size_t bit = 0U; bit < value.width; ++bit) {
            const auto word = bit / 64U;
            const auto offset = bit % 64U;
            unsigned ordinal = 0U;
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                ordinal |= static_cast<unsigned>(
                               (value.planes[plane][word] >> offset) & 1U)
                    << plane;
            }
            result.set_logic9(bit, static_cast<runtime::Logic9>(ordinal));
        }
        return result;
    }

    [[nodiscard]] const systemc::SystemCKernelChannelInventoryEntry* find_channel(
        const systemc::SystemCKernelChannelInventorySnapshot& channels,
        const systemc::SystemCEndpointId endpoint) noexcept
    {
        const auto found = std::ranges::find(
            channels.channels, endpoint,
            &systemc::SystemCKernelChannelInventoryEntry::channel);
        return found == channels.channels.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool binding_names_alias(
        const systemc::SystemCKernelBindingInventorySnapshot& bindings,
        const systemc::SystemCEndpointId endpoint,
        const std::string_view alias)
    {
        return std::ranges::any_of(bindings.bindings, [&](const auto& binding) {
            return binding.descriptor.declared_path == alias
                && std::ranges::any_of(binding.descriptor.targets,
                    [&](const auto& target) {
                        return target.final_channel == endpoint;
                    });
        });
    }

    [[nodiscard]] std::size_t checked_batch_bits(
        const std::span<const TraceObservationValue> values,
        const std::size_t limit)
    {
        std::size_t bits = 0U;
        for (const auto& value : values) {
            if (value.value.width() > limit - bits) {
                throw std::length_error(
                    "SystemC trace batch exceeds its bit limit");
            }
            bits += value.value.width();
        }
        return bits;
    }

} // namespace

struct SystemCTracePipeline::Impl {
    struct RouteSlot {
        runtime::TraceSignalId signal;
        std::size_t width { };
        bool selected { };
    };

    Impl(const runtime::TraceDeclarationModel& declaration_model,
        const systemc::SystemCKernelChannelInventorySnapshot& channels,
        const systemc::SystemCKernelBindingInventorySnapshot& bindings,
        const std::span<const SystemCTraceRoute> trace_routes,
        std::ostream& vcd_output, std::ostream& fst_output,
        const SystemCTraceLimits requested_limits)
        : declarations(&declaration_model)
        , limits(requested_limits)
        , observations(declaration_model,
              TraceObservationLimits { requested_limits.maximum_records,
                  requested_limits.maximum_values_per_batch,
                  requested_limits.maximum_bits_per_batch, 4096U, 2U })
        , vcd(vcd_output)
        , fst(fst_output)
    {
        validate_limits();
        if (channels.island != bindings.island
            || channels.hierarchy != bindings.hierarchy) {
            throw std::invalid_argument(
                "SystemC trace inventories do not describe the same hierarchy");
        }
        if (trace_routes.empty() || trace_routes.size() > limits.maximum_routes) {
            throw std::length_error("SystemC trace route count is invalid");
        }
        for (const auto& route : trace_routes) {
            add_route(channels, bindings, route);
        }
        vcd_handles = vcd.declare_model(declaration_model);
        fst.declare(declaration_model);
        vcd.begin();
        fst.begin();
        install_writers();
    }

    void validate_limits() const
    {
        if (limits.maximum_routes == 0U
            || limits.maximum_aliases_per_route == 0U
            || limits.maximum_pending_batches == 0U
            || limits.maximum_values_per_batch == 0U
            || limits.maximum_bits_per_batch == 0U
            || limits.maximum_records == 0U) {
            throw std::invalid_argument(
                "SystemC trace limits must all be positive");
        }
    }

    void add_route(
        const systemc::SystemCKernelChannelInventorySnapshot& channels,
        const systemc::SystemCKernelBindingInventorySnapshot& bindings,
        const SystemCTraceRoute& route)
    {
        const auto* channel = find_channel(channels, route.endpoint);
        if (!route.endpoint.valid() || route.signal.value == 0U
            || channel == nullptr || !channel->descriptor.supported
            || !channel->descriptor.value
            || (channel->descriptor.value->kind
                    != systemc::SystemCKernelValueKind::bit2
                && channel->descriptor.value->kind
                    != systemc::SystemCKernelValueKind::logic4)
            || channel->descriptor.observation
                == systemc::SystemCKernelObservationMode::unsupported
            || route.aliases.size() > limits.maximum_aliases_per_route) {
            throw std::invalid_argument("SystemC trace route metadata is invalid");
        }
        const auto& variable = declarations->variable(route.signal);
        const auto width = declarations->type(variable.type).width;
        if (width == 0U || width != channel->descriptor.value->width) {
            throw std::invalid_argument("SystemC trace route width is invalid");
        }
        std::set<std::uint64_t> aliases;
        for (const auto alias_id : route.aliases) {
            if (alias_id.value == 0U || !aliases.insert(alias_id.value).second) {
                throw std::invalid_argument("SystemC trace alias is invalid");
            }
            const auto& alias = declarations->alias(alias_id);
            if (alias.target != route.signal
                || !binding_names_alias(
                    bindings, route.endpoint, alias.hierarchical_name)) {
                throw std::invalid_argument(
                    "SystemC trace alias does not match the binding inventory");
            }
        }
        if (!routes.emplace(route.endpoint,
                       RouteSlot { route.signal, width, route.initially_enabled })
                .second) {
            throw std::invalid_argument("SystemC trace endpoint is duplicated");
        }
        if (!trace_signals.insert(route.signal.value).second) {
            throw std::invalid_argument(
                "SystemC trace signal has more than one source endpoint");
        }
    }

    void install_writers()
    {
        static_cast<void>(observations.add_observer([this](const auto& record) {
            for (const auto& observed : record.values) {
                fst.change({ observed.signal, record.time, record.delta,
                               record.region, record.sequence },
                    runtime::encode_fst_logic_value(observed.value));
            }
        }));
        static_cast<void>(observations.add_observer([this](const auto& record) {
            for (const auto& observed : record.values) {
                vcd.set_event({ observed.signal, record.time, record.delta,
                    record.region, record.sequence });
                vcd.begin_checkpoint("dumpall");
                vcd.change(vcd_handles.at(observed.signal.value - 1U),
                    observed.value);
                vcd.end_checkpoint();
            }
        }));
    }

    [[nodiscard]] RouteSlot& require_route(
        const systemc::SystemCEndpointId endpoint)
    {
        const auto found = routes.find(endpoint);
        if (found == routes.end()) {
            throw std::invalid_argument("SystemC trace endpoint is not routed");
        }
        return found->second;
    }

    [[nodiscard]] const RouteSlot* find_route(
        const systemc::SystemCEndpointId endpoint) const noexcept
    {
        const auto found = routes.find(endpoint);
        return found == routes.end() ? nullptr : &found->second;
    }

    void require_open() const
    {
        if (state != SystemCTraceState::open) {
            throw std::logic_error("SystemC trace pipeline is not open");
        }
    }

    void fail_writer(const std::string_view message)
    {
        state = SystemCTraceState::failed;
        code = SystemCTraceCode::writer;
        detail = message;
        pending.clear();
    }

    void validate_coordinate(
        const runtime::SimulationTick time, const std::uint64_t delta) const
    {
        if (have_coordinate
            && coordinate_precedes(time, delta, last_time, last_delta)) {
            throw std::invalid_argument(
                "SystemC trace batches must be time and delta ordered");
        }
    }

    [[nodiscard]] bool enqueue(PendingBatch batch)
    {
        if (pending.size() >= limits.maximum_pending_batches) {
            if (backpressure_events != std::numeric_limits<std::uint64_t>::max()) {
                ++backpressure_events;
            }
            code = SystemCTraceCode::backpressure;
            detail = "SystemC trace queue is applying bounded backpressure";
            return false;
        }
        last_time = batch.time;
        last_delta = batch.delta;
        have_coordinate = true;
        pending.push_back(std::move(batch));
        if (accepted_batches == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("SystemC trace batch counter exhausted");
        }
        ++accepted_batches;
        code = SystemCTraceCode::none;
        detail.clear();
        return true;
    }

    const runtime::TraceDeclarationModel* declarations;
    SystemCTraceLimits limits;
    TraceObservationRecorder observations;
    runtime::VcdWriter vcd;
    runtime::FstWriter fst;
    std::vector<runtime::VcdSignal> vcd_handles;
    std::map<systemc::SystemCEndpointId, RouteSlot> routes;
    std::set<std::uint64_t> trace_signals;
    std::deque<PendingBatch> pending;
    SystemCTraceState state { SystemCTraceState::open };
    SystemCTraceCode code { SystemCTraceCode::none };
    std::string detail;
    runtime::SimulationTick last_time { };
    std::uint64_t last_delta { };
    std::uint64_t accepted_batches { };
    std::uint64_t delivered_batches { };
    std::uint64_t backpressure_events { };
    bool have_coordinate { };
};

SystemCTracePipeline::SystemCTracePipeline(
    const runtime::TraceDeclarationModel& declarations,
    const systemc::SystemCKernelChannelInventorySnapshot& channels,
    const systemc::SystemCKernelBindingInventorySnapshot& bindings,
    const std::span<const SystemCTraceRoute> routes,
    std::ostream& vcd_output, std::ostream& fst_output,
    const SystemCTraceLimits limits)
    : impl_(std::make_unique<Impl>(declarations, channels, bindings, routes,
          vcd_output, fst_output, limits))
{
}

SystemCTracePipeline::~SystemCTracePipeline() = default;

bool SystemCTracePipeline::selected(
    const systemc::SystemCEndpointId endpoint) const noexcept
{
    const auto* route = impl_->find_route(endpoint);
    return route != nullptr && route->selected
        && impl_->state == SystemCTraceState::open;
}

bool SystemCTracePipeline::set_enabled(
    const systemc::SystemCEndpointId endpoint, const bool enable,
    const runtime::SimulationTick time, const std::uint64_t delta,
    const runtime::PackedLogic4& snapshot)
{
    impl_->require_open();
    auto& route = impl_->require_route(endpoint);
    if (!enable) {
        route.selected = false;
        return true;
    }
    if (route.selected) {
        return true;
    }
    if (snapshot.width() != route.width) {
        throw std::invalid_argument(
            "SystemC trace snapshot width does not match its route");
    }
    impl_->validate_coordinate(time, delta);
    PendingBatch batch;
    batch.time = time;
    batch.delta = delta;
    batch.region = runtime::TraceRegion::Snapshot;
    batch.identity = "systemc:late-enable-snapshot";
    batch.values.push_back({ route.signal, snapshot, std::nullopt });
    static_cast<void>(checked_batch_bits(
        batch.values, impl_->limits.maximum_bits_per_batch));
    if (!impl_->enqueue(std::move(batch))) {
        return false;
    }
    route.selected = true;
    return true;
}

bool SystemCTracePipeline::try_post_update(
    const systemc::SystemCEndpointId endpoint,
    const runtime::SimulationTick time, const std::uint64_t delta,
    const runtime::PackedLogic4& value)
{
    const SystemCTraceDirtyValue dirty { endpoint, value };
    return try_post_update_batch(time, delta, std::span { &dirty, 1U });
}

bool SystemCTracePipeline::try_post_update_batch(
    const runtime::SimulationTick time, const std::uint64_t delta,
    const std::span<const SystemCTraceDirtyValue> values)
{
    impl_->require_open();
    if (values.empty() || values.size() > impl_->limits.maximum_values_per_batch) {
        throw std::length_error("SystemC trace batch value count is invalid");
    }
    impl_->validate_coordinate(time, delta);
    std::vector<SystemCTraceDirtyValue> ordered(values.begin(), values.end());
    std::ranges::sort(ordered, { }, &SystemCTraceDirtyValue::endpoint);

    PendingBatch batch;
    batch.time = time;
    batch.delta = delta;
    batch.region = runtime::TraceRegion::Postponed;
    batch.identity = "systemc:post-update-dirty";
    systemc::SystemCEndpointId previous;
    bool have_previous = false;
    for (const auto& dirty : ordered) {
        if (have_previous && dirty.endpoint == previous) {
            throw std::invalid_argument(
                "SystemC trace batch contains a duplicate endpoint");
        }
        have_previous = true;
        previous = dirty.endpoint;
        auto& route = impl_->require_route(dirty.endpoint);
        if (!route.selected) {
            continue;
        }
        if (dirty.value.width() != route.width) {
            throw std::invalid_argument(
                "SystemC trace dirty value width does not match its route");
        }
        batch.values.push_back({ route.signal, dirty.value, std::nullopt });
    }
    if (batch.values.empty()) {
        return true;
    }
    static_cast<void>(checked_batch_bits(
        batch.values, impl_->limits.maximum_bits_per_batch));
    return impl_->enqueue(std::move(batch));
}

void SystemCTracePipeline::flush()
{
    impl_->require_open();
    try {
        while (!impl_->pending.empty()) {
            auto batch = std::move(impl_->pending.front());
            impl_->pending.pop_front();
            const auto failures = impl_->observations.callback_failures();
            static_cast<void>(impl_->observations.accept(
                TraceObservationKind::Signal, batch.time, batch.delta,
                batch.region, std::move(batch.identity), batch.values));
            if (impl_->observations.callback_failures() != failures) {
                throw std::runtime_error(
                    "SystemC trace writer rejected an accepted batch");
            }
            ++impl_->delivered_batches;
        }
        impl_->fst.flush();
        impl_->vcd.flush();
        impl_->code = SystemCTraceCode::none;
        impl_->detail.clear();
    } catch (const std::exception& error) {
        impl_->fail_writer(error.what());
        throw;
    } catch (...) {
        impl_->fail_writer("unknown SystemC trace writer failure");
        throw;
    }
}

void SystemCTracePipeline::close(const runtime::SimulationTick final_time)
{
    if (impl_->state == SystemCTraceState::closed) {
        return;
    }
    impl_->require_open();
    flush();
    try {
        impl_->fst.close(final_time);
        impl_->vcd.flush();
        impl_->state = SystemCTraceState::closed;
    } catch (const std::exception& error) {
        impl_->fail_writer(error.what());
        throw;
    } catch (...) {
        impl_->fail_writer("unknown SystemC trace close failure");
        throw;
    }
}

SystemCTraceStatus SystemCTracePipeline::status() const
{
    return { impl_->state, impl_->code, impl_->pending.size(),
        impl_->accepted_batches, impl_->delivered_batches,
        impl_->backpressure_events, impl_->detail };
}

std::span<const TraceObservationRecord>
SystemCTracePipeline::observations() const noexcept
{
    return impl_->observations.records();
}

runtime::PackedLogic4 systemc_trace_value(
    const systemc::SystemCKernelValue& value)
{
    diagnostic::Engine diagnostics;
    if (!systemc::validate_systemc_kernel_value(value, { }, diagnostics)
        || (value.kind != systemc::SystemCKernelValueKind::bit2
            && value.kind != systemc::SystemCKernelValueKind::logic4
            && value.kind != systemc::SystemCKernelValueKind::logic9)) {
        throw std::invalid_argument(
            "SystemC trace value is malformed or not waveform-compatible");
    }
    return trace_value_unchecked(value);
}

const char* systemc_trace_diagnostic_code(const SystemCTraceCode code) noexcept
{
    switch (code) {
    case SystemCTraceCode::none:
        return "";
    case SystemCTraceCode::metadata:
        return "FSIM-SC-Z001";
    case SystemCTraceCode::lifecycle:
        return "FSIM-SC-Z002";
    case SystemCTraceCode::backpressure:
        return "FSIM-SC-Z003";
    case SystemCTraceCode::writer:
        return "FSIM-SC-Z004";
    }
    return "FSIM-SC-Z001";
}

} // namespace fsim::app::application_detail
