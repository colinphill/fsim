// SPDX-License-Identifier: Apache-2.0
// A plug-in that only loads: the arrays of events of this fixture are checked
// by the design's own $display trace (IEEE 1800-2023 15.5). Lines starting
// with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

static void startup(void)
{
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };
