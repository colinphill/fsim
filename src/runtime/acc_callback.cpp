// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"

extern "C" void acc_vcl_delete(
    const handle object, PLI_INT32 (*const consumer)(p_vc_record),
    PLI_BYTE8* const user_data, const PLI_INT32 flags) {
  const bool removed = fsim::runtime::acc_detail::cancel_vcl_link(
      object, consumer, user_data, flags);
  acc_error_flag = removed ? 0 : 1;
}
