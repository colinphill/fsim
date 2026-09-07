// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/tf_instance.hpp"
#include "fsim/runtime/veriuser.h"

#include <cstdint>

namespace fsim::runtime {

inline constexpr std::uint32_t kMaxTfSynchronizationRequests = 256;

enum class TfSynchronizationKind : std::uint32_t {
  ReadWrite = 1,
  ReadOnly = 2,
};

struct TfSynchronizationRequest {
  TfSynchronizationKind kind{TfSynchronizationKind::ReadWrite};
  TfInstanceIdentity instance;
};

[[nodiscard]] bool valid_tf_synchronization_kind(
    TfSynchronizationKind kind) noexcept;
[[nodiscard]] PLI_INT32 tf_synchronization_reason(
    TfSynchronizationKind kind) noexcept;

}  // namespace fsim::runtime
