// SPDX-License-Identifier: Apache-2.0

#include "application_scv_trace.hpp"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace fsim::app::application_detail {
namespace {

    template <typename Domain>
    runtime::TransactionStableId stable(
        const systemc::SystemCBackendId<Domain> value)
    {
        return { value.high, value.low };
    }

    bool valid_region(const runtime::TransactionRegion value)
    {
        return value >= runtime::TransactionRegion::initialize
            && value <= runtime::TransactionRegion::postponed;
    }

    bool valid_limits(const ScvTraceLimits& limits)
    {
        const auto bounded = [](const std::size_t value) {
            return value != 0U
                && value <= std::numeric_limits<std::uint32_t>::max();
        };
        return bounded(limits.max_pending_records)
            && bounded(limits.max_selections)
            && bounded(limits.max_correlations_per_record)
            && bounded(limits.max_waveforms_per_record)
            && bounded(limits.max_observers);
    }

    bool coordinate_matches(const runtime::TransactionRecord& record,
        const std::uint64_t time_fs, const std::uint64_t delta)
    {
        return (record.begin_time_fs == time_fs && record.begin_delta == delta)
            || (record.end_time_fs == time_fs && record.end_delta == delta);
    }

    bool waveform_matches(const runtime::TransactionRecord& record,
        const ScvWaveformAssociation& waveform)
    {
        const auto coordinate
            = std::tuple { waveform.time_fs, waveform.delta, waveform.region };
        return coordinate
            == std::tuple { record.begin_time_fs, record.begin_delta,
                   record.begin_region }
        || coordinate
            == std::tuple {
                   record.end_time_fs, record.end_delta, record.end_region
               };
    }

} // namespace

ScvTraceCorrelationService::ScvTraceCorrelationService(ScvTraceLimits limits)
    : limits_(std::move(limits))
{
}

bool ScvTraceCorrelationService::set_selected(
    const runtime::TransactionStableId generator, const bool enabled,
    const std::uint64_t time_fs, const std::uint64_t delta,
    const runtime::TransactionRegion region, diagnostic::Engine& diagnostics)
{
    if (closed_ || !valid_limits(limits_) || !generator.valid()
        || !valid_region(region)) {
        diagnostics.error("FSIM-SCV-L001",
            "SCV trace selection identity, coordinate, limits, or state is invalid");
        return false;
    }
    if (!selections_.contains(generator)
        && selections_.size() >= limits_.max_selections) {
        diagnostics.error(
            "FSIM-SCV-L003", "SCV trace selection limit is exhausted");
        return false;
    }
    selections_.insert_or_assign(
        generator, Selection { enabled, time_fs, delta, region });
    return true;
}

std::optional<std::uint64_t> ScvTraceCorrelationService::add_observer(
    Observer observer, diagnostic::Engine& diagnostics)
{
    if (closed_ || !valid_limits(limits_) || !observer) {
        diagnostics.error(
            "FSIM-SCV-L001", "SCV trace observer or service state is invalid");
        return std::nullopt;
    }
    if (observers_.size() >= limits_.max_observers
        || next_observer_ == std::numeric_limits<std::uint64_t>::max()) {
        diagnostics.error(
            "FSIM-SCV-L003", "SCV trace observer limit is exhausted");
        return std::nullopt;
    }
    const auto token = next_observer_++;
    observers_.emplace(token, std::move(observer));
    return token;
}

bool ScvTraceCorrelationService::remove_observer(
    const std::uint64_t token, diagnostic::Engine& diagnostics)
{
    if (closed_ || token == 0U || observers_.erase(token) != 1U) {
        diagnostics.error(
            "FSIM-SCV-L001", "SCV trace observer token or service state is invalid");
        return false;
    }
    return true;
}

ScvTraceSubmitStatus ScvTraceCorrelationService::submit(
    ScvTraceSubmission submission, diagnostic::Engine& diagnostics)
{
    if (closed_ || fanning_out_ || !valid_limits(limits_)) {
        diagnostics.error(
            "FSIM-SCV-L001", "SCV trace service state or limits are invalid");
        return ScvTraceSubmitStatus::rejected;
    }
    const auto selection = selections_.find(submission.record.generator);
    if (selection == selections_.end() || !selection->second.enabled
        || std::tuple { submission.record.begin_time_fs,
               submission.record.begin_delta, submission.record.begin_region }
            < std::tuple { selection->second.time_fs, selection->second.delta,
                selection->second.region }) {
        return ScvTraceSubmitStatus::filtered;
    }
    if (last_record_
        && last_record_->transaction == submission.record.transaction) {
        diagnostics.error(
            "FSIM-SCV-L002", "SCV trace transaction is duplicated");
        return ScvTraceSubmitStatus::rejected;
    }
    if (last_record_
        && runtime::transaction_record_precedes(submission.record, *last_record_)) {
        diagnostics.error(
            "FSIM-SCV-L002", "SCV trace transaction order regressed");
        return ScvTraceSubmitStatus::rejected;
    }
    const auto correlations = static_cast<std::uint64_t>(
                                  submission.systemc_signals.size())
        + static_cast<std::uint64_t>(submission.systemc_ports.size())
        + static_cast<std::uint64_t>(submission.tlm1_activity.size())
        + static_cast<std::uint64_t>(submission.tlm2_activity.size());
    if (submission.record.correlated_objects.size()
            > limits_.max_correlations_per_record
        || correlations > limits_.max_correlations_per_record
        || submission.waveforms.size() > limits_.max_waveforms_per_record
        || correlations
            > limits_.max_correlations_per_record
                - submission.record.correlated_objects.size()) {
        diagnostics.error(
            "FSIM-SCV-L003", "SCV trace correlation or waveform limit exceeded");
        return ScvTraceSubmitStatus::rejected;
    }
    for (const auto signal : submission.systemc_signals) {
        if (!signal.valid()) {
            diagnostics.error(
                "FSIM-SCV-L002", "SCV trace signal correlation is invalid");
            return ScvTraceSubmitStatus::rejected;
        }
        submission.record.correlated_objects.push_back(
            { runtime::TransactionObjectDomain::systemc, stable(signal) });
    }
    for (const auto port : submission.systemc_ports) {
        if (!port.valid()) {
            diagnostics.error(
                "FSIM-SCV-L002", "SCV trace port correlation is invalid");
            return ScvTraceSubmitStatus::rejected;
        }
        submission.record.correlated_objects.push_back(
            { runtime::TransactionObjectDomain::systemc, stable(port) });
    }
    for (const auto& activity : submission.tlm1_activity) {
        if (!activity.transaction.valid()
            || !coordinate_matches(
                submission.record, activity.time_fs, activity.delta)) {
            diagnostics.error("FSIM-SCV-L002",
                "SCV trace TLM-1 correlation has invalid identity or time");
            return ScvTraceSubmitStatus::rejected;
        }
        submission.record.correlated_objects.push_back(
            { runtime::TransactionObjectDomain::tlm1,
                stable(activity.transaction) });
    }
    for (const auto& activity : submission.tlm2_activity) {
        if (!activity.transaction.valid()
            || !coordinate_matches(
                submission.record, activity.time_fs, activity.delta)) {
            diagnostics.error("FSIM-SCV-L002",
                "SCV trace TLM-2 correlation has invalid identity or time");
            return ScvTraceSubmitStatus::rejected;
        }
        submission.record.correlated_objects.push_back(
            { runtime::TransactionObjectDomain::tlm2,
                stable(activity.transaction) });
    }
    std::ranges::sort(submission.record.correlated_objects);
    if (std::ranges::adjacent_find(submission.record.correlated_objects)
        != submission.record.correlated_objects.end()) {
        diagnostics.error(
            "FSIM-SCV-L002", "SCV trace correlations are duplicated");
        return ScvTraceSubmitStatus::rejected;
    }
    for (const auto& waveform : submission.waveforms) {
        if ((waveform.format != ScvWaveformFormat::vcd
                && waveform.format != ScvWaveformFormat::fst)
            || waveform.signal.value == 0U || !valid_region(waveform.region)
            || !waveform_matches(submission.record, waveform)) {
            diagnostics.error("FSIM-SCV-L002",
                "SCV VCD/FST association has invalid identity or coordinate");
            return ScvTraceSubmitStatus::rejected;
        }
    }
    std::ranges::sort(
        submission.waveforms, [](const auto& left, const auto& right) {
            return std::tuple { left.time_fs, left.delta, left.region,
                left.format, left.signal.value }
            < std::tuple { right.time_fs, right.delta, right.region,
                  right.format, right.signal.value };
        });
    if (std::ranges::adjacent_find(submission.waveforms)
        != submission.waveforms.end()) {
        diagnostics.error(
            "FSIM-SCV-L002", "SCV VCD/FST associations are duplicated");
        return ScvTraceSubmitStatus::rejected;
    }
    if (!runtime::validate_transaction_record(
            submission.record, limits_.record_limits, diagnostics)) {
        diagnostics.error(
            "FSIM-SCV-L002", "SCV trace correlation produced an invalid record");
        return ScvTraceSubmitStatus::rejected;
    }
    if (pending_.size() >= limits_.max_pending_records
        || next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        diagnostics.error(
            "FSIM-SCV-L003", "SCV trace queue is applying bounded backpressure");
        return ScvTraceSubmitStatus::backpressure;
    }
    ScvTraceEnvelope envelope;
    envelope.sequence = next_sequence_++;
    envelope.record = std::move(submission.record);
    envelope.systemc_signals = std::move(submission.systemc_signals);
    envelope.systemc_ports = std::move(submission.systemc_ports);
    envelope.waveforms = std::move(submission.waveforms);
    pending_.push_back(std::move(envelope));
    last_record_ = pending_.back().record;
    fanning_out_ = true;
    for (const auto& [token, observer] : observers_) {
        static_cast<void>(token);
        try {
            observer(pending_.back());
        } catch (const std::exception& error) {
            ++callback_failures_;
            if (callback_failure_.empty())
                callback_failure_ = error.what();
        } catch (...) {
            ++callback_failures_;
            if (callback_failure_.empty())
                callback_failure_ = "unknown SCV trace observer failure";
        }
    }
    fanning_out_ = false;
    return ScvTraceSubmitStatus::accepted;
}

std::optional<ScvTraceEnvelope> ScvTraceCorrelationService::inspect(
    const runtime::TransactionStableId transaction) const
{
    const auto iterator = std::ranges::find_if(
        pending_, [transaction](const auto& envelope) {
            return envelope.record.transaction == transaction;
        });
    if (iterator == pending_.end())
        return std::nullopt;
    return *iterator;
}

std::vector<ScvTraceEnvelope> ScvTraceCorrelationService::flush()
{
    auto result = std::move(pending_);
    pending_.clear();
    return result;
}

std::vector<ScvTraceEnvelope> ScvTraceCorrelationService::close()
{
    closed_ = true;
    observers_.clear();
    return flush();
}

bool ScvTraceCorrelationService::closed() const noexcept { return closed_; }

std::size_t ScvTraceCorrelationService::pending_records() const noexcept
{
    return pending_.size();
}

std::uint64_t ScvTraceCorrelationService::callback_failures() const noexcept
{
    return callback_failures_;
}

const std::string& ScvTraceCorrelationService::callback_failure() const noexcept
{
    return callback_failure_;
}

} // namespace fsim::app::application_detail
