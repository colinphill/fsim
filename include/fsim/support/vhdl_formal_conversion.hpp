// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

// The concurrent assignment the front end makes for a port association whose
// formal part is a conversion function (IEEE 1076-2008 6.5.7.1, 14.7.3.2,
// 14.7.3.3). `none` is any other statement.
enum class VhdlFormalConversion : std::uint8_t {
    none,
    // The formal's driving value reaches the actual through the formal part's
    // function.
    driving,
    // An inout formal's effective value is its actual, converted by the
    // actual part's function.
    effective,
    last = effective,
};

} // namespace fsim::support
