// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

// Which driver of a primitive instance a process is (IEEE 1800-2023 28.3,
// 37.7): the elaboration step that splits a primitive into several
// processes records the slot, so no consumer has to read it from the
// process name. `none` is a process that drives no primitive terminal.
enum class GateDriverSlot : std::uint8_t {
    none,
    // The only driver of the instance's output.
    single,
    // The driver of the one value of a bufif or notif, whose zero value is
    // on the `single` driver.
    one,
    // The driver of output terminal `output` of a primitive with several.
    output,
    // The driver that carries the turn-on and turn-off delays of a switch
    // with a control.
    control,
    // The two conducting processes of a bidirectional switch: the one that
    // drives the first terminal from the second, and the reverse.
    switch_left,
    switch_right,
};

} // namespace fsim::support
