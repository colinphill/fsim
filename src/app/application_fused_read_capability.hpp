// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <span>

namespace fsim::app::fused_detail {

struct ReadLoweringCapability {
    bool require_direct_read_signals { };
    bool tiered_read_dedup_safe { };
};

[[nodiscard]] inline ReadLoweringCapability read_lowering_capability(
    const runtime::simir::Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds)
{
    bool has_signal_read { };
    bool direct_reads_supported = true;
    bool tiered_dedup_supported = true;
    for (const auto& stored : process.operations) {
        const auto* const read
            = runtime::simir::operation_get_if<runtime::simir::ReadSignal>(
                &stored);
        if (read == nullptr) {
            continue;
        }
        has_signal_read = true;
        if (read->kind != runtime::simir::SignalReadKind::current
            || read->signal >= signal_widths.size()
            || read->signal >= signal_value_kinds.size()
            || signal_widths[read->signal] == 0U
            || (signal_value_kinds[read->signal]
                    != runtime::simir::ValueKind::logic4
                && signal_value_kinds[read->signal]
                    != runtime::simir::ValueKind::logic9)) {
            direct_reads_supported = false;
            tiered_dedup_supported = false;
            continue;
        }
        if (signal_value_kinds[read->signal]
                != runtime::simir::ValueKind::logic4
            || signal_widths[read->signal] > 64U) {
            tiered_dedup_supported = false;
        }
    }
    const bool require_direct_reads
        = has_signal_read && direct_reads_supported;
    return {
        require_direct_reads,
        require_direct_reads && tiered_dedup_supported
    };
}

} // namespace fsim::app::fused_detail
