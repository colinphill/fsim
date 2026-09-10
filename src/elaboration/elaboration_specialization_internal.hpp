// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail::specialization_detail {

bool vhdl_composite_constant_type(const frontend::Type& type);
bool vhdl_unconstrained_builtin_array(const frontend::Type& type);
void annotate_systemverilog_constant_casts(
    frontend::Expression& expression,
    const frontend::Type& destination_type,
    const std::vector<frontend::TypeAliasDeclaration>& type_aliases);
std::optional<frontend::Expression> vital_constant_expression(
    const frontend::Expression& expression,
    const frontend::Type& type);
std::optional<std::int64_t> packed_vhdl_static_value(
    const frontend::Expression& expression,
    const frontend::Type& type,
    std::string& error);
std::string vhdl_value_identity(
    const frontend::Type& type,
    std::int64_t value);
void apply_net_delays(
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics);

} // namespace fsim::elaboration::elaboration_detail::specialization_detail
