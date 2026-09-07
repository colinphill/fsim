// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_synchronization.hpp"

namespace fsim::runtime {

bool valid_tf_synchronization_kind(
    const TfSynchronizationKind kind) noexcept {
  return kind == TfSynchronizationKind::ReadWrite ||
         kind == TfSynchronizationKind::ReadOnly;
}

PLI_INT32 tf_synchronization_reason(
    const TfSynchronizationKind kind) noexcept {
  switch (kind) {
    case TfSynchronizationKind::ReadWrite:
      return reason_synch;
    case TfSynchronizationKind::ReadOnly:
      return reason_rosynch;
  }
  return 0;
}

}  // namespace fsim::runtime
