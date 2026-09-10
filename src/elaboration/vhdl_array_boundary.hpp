// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <iosfwd>
#include <string>

namespace fsim::elaboration::elaboration_detail {
void append_vhdl_type_shape_identity(
    std::ostringstream& output,
    const frontend::Type& type);
bool vhdl_array_element_profile_matches(
    const frontend::Type& left,
    const frontend::Type& right);
bool vhdl_array_shape_matches(
    const frontend::VhdlArrayInfo& formal,
    const frontend::VhdlArrayInfo& actual,
    bool allow_unconstrained);
std::string vhdl_array_shape_identity(const frontend::Type& type);

}  // namespace fsim::elaboration::elaboration_detail
