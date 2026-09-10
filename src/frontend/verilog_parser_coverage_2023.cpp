// SPDX-License-Identifier: Apache-2.0

#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <utility>

namespace fsim::frontend {

void VerilogParser::add_covergroup_declaration(
    std::vector<SystemVerilogCovergroupDeclaration>& declarations,
    SystemVerilogCovergroupDeclaration declaration,
    const Token& start)
{
    if (declaration.name.empty()) {
        return;
    }
    const auto duplicate = std::ranges::any_of(
        declarations,
        [&](const SystemVerilogCovergroupDeclaration& existing) {
            return existing.name == declaration.name;
        });
    if (duplicate) {
        error(
            start,
            "FSIM-SV-SEM-205",
            "duplicate covergroup declaration '" + declaration.name + "'");
        return;
    }
    declarations.push_back(std::move(declaration));
}

} // namespace fsim::frontend
