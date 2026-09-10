// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"

namespace fsim::elaboration {

[[nodiscard]] frontend::Type mode_view_member_type(
    const frontend::PackedMember& member);
[[nodiscard]] bool materialize_vhdl_mode_view_endpoints(
    const frontend::Type& root_type,
    const frontend::VhdlModeViewIndication& view,
    runtime::simir::SignalId signal,
    std::size_t storage_width,
    const std::string& formal,
    const std::string& actual,
    std::vector<VhdlModeViewElementBinding>& output,
    std::string& error);

} // namespace fsim::elaboration
