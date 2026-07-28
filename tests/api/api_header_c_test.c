// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(FSIM_STRUCT_HEADER_SIZE == 8, "C ABI header changed");
_Static_assert(
    FSIM_OBJECT_INFO_V1_SIZE
        == offsetof(fsim_object_info_t, source_path),
    "C ABI object prefix changed");
_Static_assert(
    FSIM_OBJECT_FLAG_FORCED != FSIM_OBJECT_FLAG_HAS_SOURCE,
    "C ABI object flags overlap");

int main(void) {
  fsim_session_options_t options = {0};
  fsim_session_t session = FSIM_INVALID_SESSION;
  options.struct_size = FSIM_STRUCT_HEADER_SIZE;
  options.api_version = FSIM_API_VERSION;

  assert(fsim_get_api_version() == FSIM_API_VERSION);
  assert(fsim_session_create(&options, &session) == FSIM_STATUS_OK);
  assert(session != FSIM_INVALID_SESSION);
  assert(fsim_session_destroy(session) == FSIM_STATUS_OK);
  return 0;
}
