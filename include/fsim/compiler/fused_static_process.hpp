// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fsim::compiler {

/// Compiler-only representation of one certified atomic static cohort.
/// Runtime ownership and fallback remain with the original processes.
struct FusedStaticProcess {
    runtime::simir::Process process;
    std::vector<runtime::simir::ProcessId> members;
    std::size_t original_updates { };
    std::size_t aggregate_update_signals { };
};

/// Concatenate definite-assignment, callback-free static bodies. This is a
/// lowering unit for a separate post-elaboration ownership/reader graph plan;
/// acceptance here alone does not authorize private-signal routing.
[[nodiscard]] std::optional<FusedStaticProcess> fuse_static_processes(
    std::span<const runtime::simir::Process* const> members,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds,
    runtime::simir::ProcessId fused_id);

} // namespace fsim::compiler
