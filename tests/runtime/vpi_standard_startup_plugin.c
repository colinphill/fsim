// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

static unsigned startup_count;
static PLI_INT32 context_result;

static void FSIM_VPI_CALL first_startup(void) {
  s_vpi_vlog_info information = {0};
  context_result = vpi_get_vlog_info(&information);
  startup_count += 1u;
}

static void FSIM_VPI_CALL second_startup(void) {
  startup_count += 2u;
}

FSIM_VPI_EXPORT unsigned FSIM_VPI_CALL
fsim_vpi_standard_startup_count(void) {
  return startup_count;
}

FSIM_VPI_EXPORT PLI_INT32 FSIM_VPI_CALL
fsim_vpi_standard_context_result(void) {
  return context_result;
}

FSIM_VPI_EXPORT fsim_vpi_startup_routine_v1 vlog_startup_routines[] = {
    &first_startup,
    &second_startup,
    0,
};
