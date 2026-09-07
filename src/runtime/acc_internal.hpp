// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/acc_handle_bridge.h"

#include <optional>
#include <string>
#include <string_view>

namespace fsim::runtime::acc_detail {

[[nodiscard]] const fsim_acc_handle_context_v3* current_context() noexcept;
[[nodiscard]] const fsim_acc_tf_context_v3* current_tf_context() noexcept;
[[nodiscard]] bool valid_tf_context_binding(
    const fsim_acc_handle_context_v3* context) noexcept;
[[nodiscard]] bool type_matches(PLI_INT32 actual,
                                PLI_INT32 requested) noexcept;
[[nodiscard]] bool configuration_enabled(
    PLI_INT32 item, std::string_view routine) noexcept;
[[nodiscard]] std::optional<std::string> configuration_value(
    PLI_INT32 item) noexcept;
[[nodiscard]] bool cancel_vcl_link(
    handle object, PLI_INT32 (*consumer)(p_vc_record),
    PLI_BYTE8* user_data, PLI_INT32 flags) noexcept;
[[nodiscard]] bool invalidate_iterator_safe_point(
    const fsim_acc_handle_context_v3* context) noexcept;
void reset_read_borrowed_storage() noexcept;
void reset_write_borrowed_storage() noexcept;

}  // namespace fsim::runtime::acc_detail
