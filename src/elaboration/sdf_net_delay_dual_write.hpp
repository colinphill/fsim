// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <cstddef>
#include <utility>
#include <vector>

// The pieces of a dual write: a writer of a net keeps writing the net and
// also writes a shadow signal, and nothing is rerouted. Shared by the split
// of SDF net delays and the multiple source mode of VITAL wire delays.
namespace fsim::elaboration::dual_write {

/// The signal a write operation drives, or null when `operation` is not a
/// write.
[[nodiscard]] runtime::simir::SignalId* write_target(
    runtime::simir::Operation& operation);

/// The positions of the operations of `process` that write `net`. False when
/// the process connects the net through a switch, or (unless `allow_reads`)
/// also reads the net or has an operation that names signals in a way the
/// dual write does not cover. `allow_vital_delay` accepts a VitalDelay that
/// drives the net as a write. `allow_reads` accepts a writer that reads,
/// waits on or is sensitive to the net: the dual write copies what the
/// process writes, which is its contribution whatever it computed the value
/// from, and the process keeps reading the real net.
[[nodiscard]] bool net_writes(const runtime::simir::Process& process,
    runtime::simir::SignalId net, std::vector<std::size_t>& positions,
    bool allow_vital_delay = false, bool allow_reads = false);

/// Puts each operation of `inserted` right after the operation at its
/// position (positions ascending) and moves every instruction index of the
/// process to match.
void insert_after(runtime::simir::Process& process,
    const std::vector<std::pair<std::size_t, runtime::simir::Operation>>&
        inserted);

} // namespace fsim::elaboration::dual_write
