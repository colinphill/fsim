// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fsim::elaboration {

struct SdfNetDelaySplitResult {
    /// The load ports whose net was split and now carry a delay.
    std::size_t split { };
    /// One reason for each annotated load that was left undelayed.
    std::vector<std::string> skipped;
};

/// Makes the INTERCONNECT and PORT delays of `state.sdf_net_delays` delay the
/// net in simulation (IEEE 1364-2005 16 SDF back annotation, IEEE
/// 1800-2023 38.10, 32). Port connections share one signal between the driver
/// side and the instance behind the port, so each annotated load port gets a
/// signal of its own: a copy of the net that a new process writes with the
/// delay's rise, fall and turn-off transition delays (an inertial,
/// continuous-assignment write, like a primitive's output delay), and the
/// processes of the load's instance read the copy instead of the net. Only
/// the load side sees the delayed value. A design without an annotated port
/// is not changed. Appends signals and processes, so existing ids stay valid,
/// and resets the signal driver inventory.
SdfNetDelaySplitResult split_sdf_net_delays(ElaboratedDesignState& state);

} // namespace fsim::elaboration
