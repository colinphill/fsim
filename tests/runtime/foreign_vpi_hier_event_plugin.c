// SPDX-License-Identifier: Apache-2.0
// A VPI library that registers nothing: it makes a run a foreign run, for
// designs whose SDF-delayed behavior the design itself prints.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

static void startup(void) { }

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };
