// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/runtime/trace_model.hpp"
#include "fsim/runtime/transaction_record.hpp"
#include "fsim/systemc/kernel_backend_protocol.hpp"
#include "fsim/systemc/kernel_backend_tlm1.hpp"
#include "fsim/systemc/kernel_backend_tlm2.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fsim::app::application_detail {

enum class ScvWaveformFormat : std::uint8_t {
    vcd = 1,
    fst = 2,
};

struct ScvWaveformAssociation {
    ScvWaveformFormat format { ScvWaveformFormat::vcd };
    runtime::TraceSignalId signal;
    std::uint64_t time_fs { };
    std::uint64_t delta { };
    runtime::TransactionRegion region { runtime::TransactionRegion::evaluate };
    friend bool operator==(
        const ScvWaveformAssociation&, const ScvWaveformAssociation&) = default;
};

struct ScvTraceSubmission {
    runtime::TransactionRecord record;
    std::vector<systemc::SystemCObjectId> systemc_signals;
    std::vector<systemc::SystemCEndpointId> systemc_ports;
    std::vector<systemc::SystemCKernelTlm1Transaction> tlm1_activity;
    std::vector<systemc::SystemCKernelTlm2Transaction> tlm2_activity;
    std::vector<ScvWaveformAssociation> waveforms;
};

struct ScvTraceEnvelope {
    std::uint64_t sequence { };
    runtime::TransactionRecord record;
    std::vector<systemc::SystemCObjectId> systemc_signals;
    std::vector<systemc::SystemCEndpointId> systemc_ports;
    std::vector<ScvWaveformAssociation> waveforms;
    friend bool operator==(const ScvTraceEnvelope&, const ScvTraceEnvelope&) = default;
};

struct ScvTraceLimits {
    std::size_t max_pending_records { 65536U };
    std::size_t max_selections { 4096U };
    std::size_t max_correlations_per_record { 4096U };
    std::size_t max_waveforms_per_record { 4096U };
    std::size_t max_observers { 64U };
    runtime::TransactionRecordLimits record_limits;
};

enum class ScvTraceSubmitStatus : std::uint8_t {
    accepted = 1,
    filtered = 2,
    backpressure = 3,
    rejected = 4,
};

class ScvTraceCorrelationService {
public:
    using Observer = std::function<void(const ScvTraceEnvelope&)>;

    explicit ScvTraceCorrelationService(ScvTraceLimits limits = { });

    [[nodiscard]] bool set_selected(
        runtime::TransactionStableId generator, bool enabled,
        std::uint64_t time_fs, std::uint64_t delta,
        runtime::TransactionRegion region, diagnostic::Engine& diagnostics);
    [[nodiscard]] std::optional<std::uint64_t> add_observer(
        Observer observer, diagnostic::Engine& diagnostics);
    [[nodiscard]] bool remove_observer(
        std::uint64_t token, diagnostic::Engine& diagnostics);
    [[nodiscard]] ScvTraceSubmitStatus submit(
        ScvTraceSubmission submission, diagnostic::Engine& diagnostics);
    [[nodiscard]] std::optional<ScvTraceEnvelope> inspect(
        runtime::TransactionStableId transaction) const;
    [[nodiscard]] std::vector<ScvTraceEnvelope> flush();
    [[nodiscard]] std::vector<ScvTraceEnvelope> close();

    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] std::size_t pending_records() const noexcept;
    [[nodiscard]] std::uint64_t callback_failures() const noexcept;
    [[nodiscard]] const std::string& callback_failure() const noexcept;

private:
    struct Selection {
        bool enabled { };
        std::uint64_t time_fs { };
        std::uint64_t delta { };
        runtime::TransactionRegion region { runtime::TransactionRegion::evaluate };
    };

    ScvTraceLimits limits_;
    std::map<runtime::TransactionStableId, Selection> selections_;
    std::map<std::uint64_t, Observer> observers_;
    std::vector<ScvTraceEnvelope> pending_;
    std::optional<runtime::TransactionRecord> last_record_;
    std::uint64_t next_observer_ { 1U };
    std::uint64_t next_sequence_ { 1U };
    std::uint64_t callback_failures_ { };
    std::string callback_failure_;
    bool fanning_out_ { };
    bool closed_ { };
};

} // namespace fsim::app::application_detail
