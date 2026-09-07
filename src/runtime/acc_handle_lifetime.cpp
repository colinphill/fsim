// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

namespace {

using ContextIdentity = std::pair<std::uint64_t, std::uint64_t>;

std::mutex safe_point_mutex;
std::map<ContextIdentity, std::uint64_t> safe_points;

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

}  // namespace

extern "C" PLI_INT32 FSIM_NATIVE_PLUGIN_CALL fsim_acc_safe_point_advance_v3(
    fsim_acc_handle_context_v3* const context,
    const fsim_acc_safe_point_v3* const safe_point) {
  if (context == nullptr ||
      context != fsim::runtime::acc_detail::current_context() ||
      safe_point == nullptr ||
      fsim::runtime::validate_tf_native_pointer(
          safe_point, sizeof(*safe_point),
          fsim::runtime::TfNativePointerAccess::Read) !=
          fsim::runtime::TfContainmentError::None ||
      safe_point->abi_version != FSIM_ACC_SAFE_POINT_ABI_VERSION ||
      safe_point->struct_size < sizeof(*safe_point) ||
      safe_point->reserved != 0 || safe_point->reserved2 != 0 ||
      safe_point->identity == 0) {
    publish_error(true);
    return 0;
  }

  const ContextIdentity identity{context->simulation_identity,
                                 context->hierarchy_generation};
  std::optional<std::uint64_t> previous;
  try {
    std::scoped_lock lock{safe_point_mutex};
    const auto found = safe_points.find(identity);
    if (found != safe_points.end()) {
      if (safe_point->identity <= found->second) {
        publish_error(true);
        return 0;
      }
      previous = found->second;
      found->second = safe_point->identity;
    } else {
      safe_points.emplace(identity, safe_point->identity);
    }
  } catch (...) {
    publish_error(true);
    return 0;
  }

  if (!fsim::runtime::acc_detail::invalidate_iterator_safe_point(context)) {
    std::scoped_lock lock{safe_point_mutex};
    if (previous.has_value()) {
      safe_points[identity] = *previous;
    } else {
      safe_points.erase(identity);
    }
    publish_error(true);
    return 0;
  }
  fsim::runtime::acc_detail::reset_read_borrowed_storage();
  fsim::runtime::acc_detail::reset_write_borrowed_storage();
  publish_error(false);
  return 1;
}
