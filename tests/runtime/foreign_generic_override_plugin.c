// SPDX-License-Identifier: Apache-2.0
// A VPI and VHPI library that registers nothing: the top-level generic and
// parameter override cases print the values the design was elaborated with.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "fsim/runtime/vpi_abi.h"
#include "vhpi_user.h"
#include "vpi_user.h"

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { 0 };
FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { 0 };
