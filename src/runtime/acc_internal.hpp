// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/acc_handle_bridge.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

struct t_cb_data;

namespace fsim::runtime::acc_detail {

// Publishes acc_error_flag and, for an error, displays it unless
// accDisplayErrors is "false" (IEEE 1364-2001 22.7).
void report_error(bool failed, std::string_view routine) noexcept;
// Displays a warning when accDisplayWarnings is "true" (22.7.2).
void report_warning(std::string_view routine,
                    std::string_view message) noexcept;
// The routine reported is the function that called, found at the call site.
inline void publish_error(
    const bool failed,
    const std::source_location where = std::source_location::current())
    noexcept {
  report_error(failed, where.function_name());
}

// vpi_control(vpiSetInteractiveScope) changed the interactive scope to the
// VPI object; acc_handle_interactive_scope reports it until a later
// acc_set_interactive_scope.
void note_interactive_scope(std::uint64_t object) noexcept;

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
// The value change callback of an ACC link (vpi_register_cb): its object's
// changes keep their values until dispatched, so deliver_vcl_changes can
// hand them over early. Defined over the VPI (vpi_standard.cpp).
[[nodiscard]] PLI_UINT32* register_vcl_callback(t_cb_data* callback);
// Delivers to the link's callback, now, the changes observed and not yet
// dispatched (acc_vcl_delete, IEEE 1364-2001 23.97, 23.98).
[[nodiscard]] bool deliver_vcl_changes(PLI_UINT32* callback);
// Suspends the running routine's TF and ACC call contexts, so a consumer
// can run in its own call site's contexts, and resumes them when the
// returned guard is released (tf_link.cpp, acc_handle.cpp).
[[nodiscard]] std::shared_ptr<void> suspend_tf_call();
[[nodiscard]] std::shared_ptr<void> suspend_acc_call();
[[nodiscard]] bool invalidate_iterator_safe_point(
    const fsim_acc_handle_context_v3* context) noexcept;
void reset_read_borrowed_storage() noexcept;
void reset_write_borrowed_storage() noexcept;

}  // namespace fsim::runtime::acc_detail
