// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_synchronization.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
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

void lifecycle_request(SystemCKernelBackend& backend,
    const SystemCKernelDirectRequest& request)
{
    const auto result = backend.request(request);
    assert(result.status == SystemCKernelDirectResultStatus::ok);
    const auto* receipt
        = std::get_if<SystemCKernelLifecycleReceipt>(&result.receipt);
    assert(receipt && receipt->code == SystemCKernelLifecycleCode::none);
}

LiveSession make_live_session(const std::filesystem::path& plugin,
    const std::string& identity, const std::string& root,
    const SystemCKernelHostLanguage host_language,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits)
{
    fsim::diagnostic::Engine diagnostics;
    auto backend = make_systemc_kernel_session_backend(protocol_limits,
        session_limits, execution_limits, diagnostics);
    assert(backend && diagnostics.diagnostics().empty());
    const auto ids = make_ids(identity, root, protocol_limits);
    lifecycle_request(*backend, SystemCKernelCreateSessionRequest { ids.island,
        { 1U }, { identity, plugin.string(), 1U } });
    lifecycle_request(*backend, SystemCKernelCreateObjectRequest { ids.island,
        { 2U }, ids.hierarchy, ids.object,
        { "synchronized-roots", root, "execution_root", root, { } } });
    lifecycle_request(*backend, SystemCKernelBindEndpointRequest { ids.island,
        { 3U }, ids.hierarchy, ids.object, ids.input,
        { root + ".input", root + ".input_channel" } });
    lifecycle_request(*backend, SystemCKernelBindEndpointRequest { ids.island,
        { 4U }, ids.hierarchy, ids.object, ids.output,
        { root + ".output", root + ".output_channel" } });
    lifecycle_request(
        *backend, SystemCKernelElaborateRequest { ids.island, { 5U } });
    lifecycle_request(
        *backend, SystemCKernelStartRequest { ids.island, { 6U } });
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
    std::size_t requests { };
    std::size_t closes { };
};

class DisconnectBackend final : public SystemCKernelBackend {
public:
    explicit DisconnectBackend(std::shared_ptr<DisconnectState> state)
        : state_ { std::move(state) }
    {
    }

    [[nodiscard]] SystemCKernelDirectResult request(
        const SystemCKernelDirectRequest&) noexcept override
    {
        ++state_->requests;
        return { SystemCKernelDirectResultStatus::disconnected, { } };
    }

    void close() noexcept override { ++state_->closes; }

private:
    std::shared_ptr<DisconnectState> state_;
};

enum class InvalidReceiptMode {
    lifecycle_alternative,
    inconsistent_activity,
};

struct InvalidReceiptState {
    std::size_t requests { };
    std::size_t closes { };
};

class InvalidReceiptBackend final : public SystemCKernelBackend {
public:
    InvalidReceiptBackend(const InvalidReceiptMode mode,
        std::shared_ptr<InvalidReceiptState> state)
        : mode_ { mode }
        , state_ { std::move(state) }
    {
    }

    [[nodiscard]] SystemCKernelDirectResult request(
        const SystemCKernelDirectRequest& request) noexcept override
    {
        ++state_->requests;
        if (mode_ == InvalidReceiptMode::lifecycle_alternative) {
            return { SystemCKernelDirectResultStatus::ok,
                SystemCKernelLifecycleReceipt { } };
        }

        const auto island = std::visit(
            [](const auto& operation) { return operation.island; }, request);
        SystemCKernelExecutionReceipt receipt;
        receipt.session_state = SystemCKernelSessionState::quiescent;
        receipt.status = SystemCKernelExecutionStatus::quiescent;
        receipt.published = true;
        receipt.future_activity = true;
        receipt.order = { 0U, 0U, SystemCAccelleraRegion::quiescent,
            island, { 1U } };
        return { SystemCKernelDirectResultStatus::ok, std::move(receipt) };
    }

    void close() noexcept override { ++state_->closes; }

private:
    InvalidReceiptMode mode_ { };
    std::shared_ptr<InvalidReceiptState> state_;
};

void audit_invalid_receipt_containment(
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelExecutionLimits& execution_limits)
{
    for (const auto mode : { InvalidReceiptMode::lifecycle_alternative,
             InvalidReceiptMode::inconsistent_activity }) {
        fsim::diagnostic::Engine diagnostics;
        auto synchronizer = make_systemc_kernel_synchronizer(protocol_limits,
            execution_limits, { }, diagnostics);
        assert(synchronizer);
        const auto island = make_ids(
            "invalid-receipt-island", "invalid_receipt_root", protocol_limits);
        const auto second_island = make_ids("second-invalid-receipt-island",
            "second_invalid_receipt_root", protocol_limits);
        auto state = std::make_shared<InvalidReceiptState>();
        auto second_state = std::make_shared<InvalidReceiptState>();
        assert(synchronizer->attach_island(
            { island.island, SystemCKernelHostLanguage::verilog, { 1U }, 0U,
                { } },
            std::make_unique<InvalidReceiptBackend>(mode, state), diagnostics));
        assert(synchronizer->attach_island(
            { second_island.island, SystemCKernelHostLanguage::system_verilog,
                { 1U }, 0U, { } },
            std::make_unique<InvalidReceiptBackend>(mode, second_state),
            diagnostics));

        assert(!synchronizer->synchronize({ 0U, 0U }, { }, diagnostics));
        assert(has_code(diagnostics, "FSIM-SC-N004"));
        assert(synchronizer->failed());
        assert(synchronizer->island_count() == 0U);
        assert(state->requests + second_state->requests == 1U);
        assert(state->closes == 1U && second_state->closes == 1U);
    }
}

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
    const auto second_island = make_ids(
        "second-disconnect-island", "second_disconnect_root", protocol_limits);
    auto state = std::make_shared<DisconnectState>();
    auto second_state = std::make_shared<DisconnectState>();
    assert(synchronizer->attach_island(
        { island.island, SystemCKernelHostLanguage::verilog, { 1U }, 0U,
            { } },
        std::make_unique<DisconnectBackend>(state), diagnostics));
    assert(synchronizer->attach_island(
        { second_island.island, SystemCKernelHostLanguage::system_verilog,
            { 1U }, 0U, { } },
        std::make_unique<DisconnectBackend>(second_state), diagnostics));
    assert(!synchronizer->synchronize({ 0U, 0U }, { }, diagnostics));
    assert(has_code(diagnostics, "FSIM-SC-N004"));
    assert(synchronizer->failed());
    assert(synchronizer->island_count() == 0U);
    assert(state->requests + second_state->requests == 1U);
    assert(state->closes == 1U && second_state->closes == 1U);

    diagnostics = { };
    auto recovered = make_systemc_kernel_synchronizer(protocol_limits,
        execution_limits, { }, diagnostics);
    assert(recovered && !recovered->failed());
    assert(recovered->island_count() == 0U);

    audit_invalid_receipt_containment(protocol_limits, execution_limits);
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
        SystemCKernelHostLanguage::system_verilog, protocol_limits,
        session_limits, execution_limits);
    auto vhdl = make_live_session(plugin, "vhdl-systemc-island", "vhdl_root",
        SystemCKernelHostLanguage::vhdl, protocol_limits, session_limits,
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
