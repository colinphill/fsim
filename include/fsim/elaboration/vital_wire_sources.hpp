// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <span>
#include <string>
#include <vector>

namespace fsim::elaboration {

struct VitalWireSourcesResult {
    /// One reason for each interconnect delay generic whose multiple source
    /// mode could not be set up; each is an error.
    std::vector<std::string> failures;
};

/// Sets up the multiple source mode of IEEE 1076.4-2000 5.2.7.1.5 and 6.4.1
/// for the VitalWireDelay calls whose interconnect delay generic has the
/// delay locations of `locations`. The actual of the call's InSig port has
/// the drivers of its net; each source that an SDF INTERCONNECT entry names
/// gets the drivers of its instance, and the drivers that no entry names are
/// one more source with the generic's value. Each driver keeps writing the
/// net and also writes a shadow signal of its source (a dual write; nothing
/// is rerouted), and the call selects, on every event on its input, the
/// sources whose shadow had an event in the delta cycle (VitalDelay::
/// wire_sources). Appends signals, so existing ids stay valid, and resets
/// the signal driver inventory. A driver that cannot be identified, or whose
/// writes the dual write does not cover, is a failure; nothing is
/// approximated.
VitalWireSourcesResult apply_vital_wire_sources(ElaboratedDesignState& state,
    std::span<const VitalWireLocations> locations);

} // namespace fsim::elaboration
