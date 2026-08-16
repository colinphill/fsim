// SPDX-License-Identifier: Apache-2.0

#include "application_scv_trace.hpp"

#include <cassert>
#include <stdexcept>
#include <string_view>

namespace {

bool has_code(
    const fsim::diagnostic::Engine& diagnostics, const std::string_view code)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == code)
            return true;
    }
    return false;
}

fsim::runtime::TransactionRecord record(
    const std::uint64_t time, const std::uint64_t transaction)
{
    using namespace fsim::runtime;
    TransactionRecord result;
    result.stream = { 1U, 1U };
    result.generator = { 2U, 2U };
    result.transaction = { 3U, transaction };
    result.begin_time_fs = time;
    result.begin_delta = 1U;
    result.begin_region = TransactionRegion::evaluate;
    result.end_time_fs = time;
    result.end_delta = 1U;
    result.end_region = TransactionRegion::update;
    return result;
}

fsim::app::application_detail::ScvTraceSubmission submission(
    const std::uint64_t time, const std::uint64_t transaction)
{
    using namespace fsim::app::application_detail;
    ScvTraceSubmission result;
    result.record = record(time, transaction);
    result.systemc_signals.push_back({ 10U, transaction });
    result.systemc_ports.push_back({ 11U, transaction });
    fsim::systemc::SystemCKernelTlm1Transaction tlm1;
    tlm1.transaction = { 12U, transaction };
    tlm1.time_fs = time;
    tlm1.delta = 1U;
    result.tlm1_activity.push_back(tlm1);
    fsim::systemc::SystemCKernelTlm2Transaction tlm2;
    tlm2.transaction = { 13U, transaction };
    tlm2.time_fs = time;
    tlm2.delta = 1U;
    result.tlm2_activity.push_back(tlm2);
    result.waveforms.push_back({ ScvWaveformFormat::vcd,
        { 100U + transaction }, time, 1U,
        fsim::runtime::TransactionRegion::evaluate });
    result.waveforms.push_back({ ScvWaveformFormat::fst,
        { 100U + transaction }, time, 1U,
        fsim::runtime::TransactionRegion::update });
    return result;
}

} // namespace

int main()
{
    using namespace fsim::app::application_detail;
    using fsim::runtime::TransactionRegion;

    fsim::diagnostic::Engine diagnostics;
    ScvTraceCorrelationService service;
    std::uint64_t callback_sequence { };
    const auto observer = service.add_observer(
        [&](const ScvTraceEnvelope& envelope) {
            assert(envelope.sequence > callback_sequence);
            callback_sequence = envelope.sequence;
        },
        diagnostics);
    const auto throwing = service.add_observer(
        [](const ScvTraceEnvelope&) { throw std::runtime_error("observer"); },
        diagnostics);
    assert(observer && throwing);
    assert(service.set_selected(
        { 2U, 2U }, true, 10U, 1U, TransactionRegion::evaluate, diagnostics));
    assert(service.submit(submission(9U, 1U), diagnostics)
        == ScvTraceSubmitStatus::filtered);
    assert(service.pending_records() == 0U);
    assert(service.submit(submission(10U, 2U), diagnostics)
        == ScvTraceSubmitStatus::accepted);
    assert(service.pending_records() == 1U);
    assert(callback_sequence == 1U);
    assert(service.callback_failures() == 1U);
    assert(service.callback_failure() == "observer");
    const auto inspected = service.inspect({ 3U, 2U });
    assert(inspected && inspected->record.correlated_objects.size() == 4U);
    assert(inspected->systemc_signals.size() == 1U);
    assert(inspected->systemc_ports.size() == 1U);
    assert(inspected->waveforms.size() == 2U);
    assert(inspected->waveforms[0].format == ScvWaveformFormat::vcd);
    diagnostics.clear();
    assert(service.submit(submission(10U, 2U), diagnostics)
        == ScvTraceSubmitStatus::rejected);
    assert(has_code(diagnostics, "FSIM-SCV-L002"));
    assert(service.pending_records() == 1U);
    assert(service.remove_observer(*throwing, diagnostics));

    auto limits = ScvTraceLimits { };
    limits.max_pending_records = 1U;
    ScvTraceCorrelationService bounded(limits);
    assert(bounded.set_selected(
        { 2U, 2U }, true, 0U, 0U, TransactionRegion::initialize, diagnostics));
    assert(bounded.submit(submission(10U, 10U), diagnostics)
        == ScvTraceSubmitStatus::accepted);
    diagnostics.clear();
    assert(bounded.submit(submission(11U, 11U), diagnostics)
        == ScvTraceSubmitStatus::backpressure);
    assert(has_code(diagnostics, "FSIM-SCV-L003"));
    assert(bounded.pending_records() == 1U);
    auto flushed = bounded.flush();
    assert(flushed.size() == 1U && bounded.pending_records() == 0U);
    diagnostics.clear();
    assert(bounded.submit(submission(11U, 11U), diagnostics)
        == ScvTraceSubmitStatus::accepted);

    auto bad_tlm = submission(12U, 12U);
    bad_tlm.tlm2_activity[0].time_fs = 99U;
    diagnostics.clear();
    assert(bounded.submit(std::move(bad_tlm), diagnostics)
        == ScvTraceSubmitStatus::rejected);
    assert(has_code(diagnostics, "FSIM-SCV-L002"));
    assert(bounded.pending_records() == 1U);
    auto closed = bounded.close();
    assert(closed.size() == 1U && bounded.closed());
    diagnostics.clear();
    assert(bounded.submit(submission(13U, 13U), diagnostics)
        == ScvTraceSubmitStatus::rejected);
    assert(has_code(diagnostics, "FSIM-SCV-L001"));
}
