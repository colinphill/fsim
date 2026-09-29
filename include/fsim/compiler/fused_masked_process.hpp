// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fsim::compiler {

struct FusedMaskedMemberGate {
    runtime::simir::ProcessId original_id { };
    runtime::simir::InstructionIndex begin_instruction { };
    runtime::simir::InstructionIndex end_instruction { };
    std::uint32_t activation_bit { };
    friend bool operator==(const FusedMaskedMemberGate&,
        const FusedMaskedMemberGate&) = default;
};

struct FusedMaskedMemberWrites {
    runtime::simir::ProcessId original_id { };
    std::vector<runtime::simir::Process::DriverRegion> regions;
};

/// A native lowering unit, not permission to change scheduler dependencies.
/// Every selected member reads the original committed input snapshot.
struct FusedMaskedProcess {
    runtime::simir::Process process;
    std::vector<FusedMaskedMemberGate> gates;
    std::vector<FusedMaskedMemberWrites> writes;
};

[[nodiscard]] std::optional<FusedMaskedProcess> fuse_masked_processes(
    std::span<const runtime::simir::Process* const> ordered_members,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds,
    runtime::simir::ProcessId synthetic_id);

} // namespace fsim::compiler
