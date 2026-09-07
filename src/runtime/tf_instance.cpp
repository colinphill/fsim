// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_instance.hpp"

namespace fsim::runtime {

TfInstanceError validate_tf_instance_identity(
    const TfInstanceIdentity& identity) noexcept {
  if (identity.design_id == 0) {
    return TfInstanceError::DesignIdentity;
  }
  if (identity.hierarchy_id == 0) {
    return TfInstanceError::HierarchyIdentity;
  }
  if (identity.generation == 0) {
    return TfInstanceError::Generation;
  }
  return TfInstanceError::None;
}

}  // namespace fsim::runtime
