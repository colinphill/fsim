// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration::vhdl_configuration_detail {

std::string configuration_canonical_name(std::string_view);
bool configuration_name_equal(std::string_view, std::string_view);
std::vector<std::string> configuration_name_parts(std::string_view);

std::string configuration_expression_identity(
    const semantic::CompiledDesign&, semantic::ExpressionId);
std::optional<std::int64_t> configuration_static_integer(
    const semantic::CompiledDesign&, semantic::ExpressionId);

std::optional<std::string> configuration_block_scope(
    const semantic::CompiledDesign&,
    const semantic::vhdl::BlockConfiguration&);

std::optional<std::vector<std::string>> configuration_occurrence_parts(
    const semantic::CompiledDesign&,
    const semantic::vhdl::Unit&,
    const semantic::vhdl::Instance&,
    std::string_view occurrence_path);

} // namespace fsim::elaboration::vhdl_configuration_detail
