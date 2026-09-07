// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::runtime {

struct TfInstanceIdentity {
  std::uint64_t design_id{1};
  std::uint64_t hierarchy_id{1};
  std::uint32_t generation{1};

  [[nodiscard]] friend bool operator==(
      const TfInstanceIdentity&, const TfInstanceIdentity&) = default;
};

enum class TfInstanceError {
  None,
  DesignIdentity,
  HierarchyIdentity,
  Generation,
};

[[nodiscard]] TfInstanceError validate_tf_instance_identity(
    const TfInstanceIdentity& identity) noexcept;

}  // namespace fsim::runtime
