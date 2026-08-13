// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

// FSIM-CONFORMANCE CF-COMMON-CABI-001 source=SRC-COCOTB expectation=accept
_Static_assert(FSIM_STRUCT_HEADER_SIZE == 8, "C ABI header changed");
_Static_assert(
    FSIM_OBJECT_INFO_V1_SIZE
        == offsetof(fsim_object_info_t, source_path),
    "C ABI object prefix changed");
_Static_assert(
    FSIM_OBJECT_FLAG_FORCED != FSIM_OBJECT_FLAG_HAS_SOURCE,
    "C ABI object flags overlap");
_Static_assert(
    FSIM_OBJECT_FLAG_INITIALIZED != FSIM_OBJECT_FLAG_ENTERED,
    "C ABI debug-state flags overlap");
_Static_assert(
    FSIM_OBJECT_FLAG_RESOLVED != FSIM_OBJECT_FLAG_FORCED
        && FSIM_OBJECT_FLAG_RESOLVED != FSIM_OBJECT_FLAG_HAS_SOURCE
        && FSIM_OBJECT_FLAG_RESOLVED != FSIM_OBJECT_FLAG_INITIALIZED
        && FSIM_OBJECT_FLAG_RESOLVED != FSIM_OBJECT_FLAG_ENTERED
        && FSIM_OBJECT_FLAG_HAS_PROVENANCE != FSIM_OBJECT_FLAG_FORCED
        && FSIM_OBJECT_FLAG_HAS_PROVENANCE != FSIM_OBJECT_FLAG_HAS_SOURCE
        && FSIM_OBJECT_FLAG_HAS_PROVENANCE != FSIM_OBJECT_FLAG_RESOLVED,
    "C ABI resolved flag overlaps");
_Static_assert(
    FSIM_CALLBACKS_V1_SIZE
        == offsetof(fsim_callbacks_t, safe_point_info),
    "C ABI callback prefix changed");

int main(void) {
  fsim_session_options_t options = {0};
  fsim_session_t session = FSIM_INVALID_SESSION;
  options.struct_size = FSIM_STRUCT_HEADER_SIZE;
  options.api_version = FSIM_API_VERSION;

  assert(FSIM_SCHEDULER_PHASE_UPDATE == 3);
  assert(FSIM_SCHEDULER_PHASE_POSTPONED == 4);
  assert(FSIM_SCHEDULER_PHASE_REACTIVE == 5);
  assert(FSIM_SCHEDULER_PHASE_OBSERVED == 6);
  assert(FSIM_SCHEDULER_PHASE_RE_INACTIVE == 7);
  assert(FSIM_SCHEDULER_PHASE_RE_UPDATE == 8);

  assert(fsim_get_api_version() == FSIM_API_VERSION);
  assert(fsim_session_create(&options, &session) == FSIM_STATUS_OK);
  assert(session != FSIM_INVALID_SESSION);
  assert(fsim_session_destroy(session) == FSIM_STATUS_OK);
  return 0;
}
