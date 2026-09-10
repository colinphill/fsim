// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace fsim::elaboration::vhdl_component_detail {
std::string canonical_generic_name(
    std::string_view name,
    const std::unordered_map<std::string, std::string>& names);

std::string expression_profile(
    const frontend::Expression& expression,
    const std::unordered_map<std::string, std::string>& names);

std::string type_profile(
    const frontend::Type& type,
    const std::unordered_map<std::string, std::string>& names);

}  // namespace fsim::elaboration::vhdl_component_detail
