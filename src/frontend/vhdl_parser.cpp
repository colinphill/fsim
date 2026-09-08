// SPDX-License-Identifier: Apache-2.0
#include "vhdl_conditional_analysis_internal.hpp"
#include "vhdl_parser_internal.hpp"

#include <algorithm>
#include <iterator>

namespace fsim::frontend {

ParseResult parse_vhdl(SourceText source, const VhdlStandard vhdl_standard)
{
    auto conditional = analyze_vhdl_conditionals(std::move(source), vhdl_standard);
    const bool conditional_profile_compatible = std::ranges::none_of(
        conditional.diagnostics, [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-VHDL-CA-001"
                || diagnostic.code == "FSIM-VHDL-PROTECT-001";
        });
    auto lexed = lex(
        std::move(conditional.source), Language::Vhdl2008, vhdl_standard);
    lexed.diagnostics.insert(lexed.diagnostics.begin(),
        std::make_move_iterator(conditional.diagnostics.begin()),
        std::make_move_iterator(conditional.diagnostics.end()));
    auto result = VhdlParser(std::move(lexed), vhdl_standard).run();
    result.design.vhdl_profile_compatible =
        result.design.vhdl_profile_compatible
        && conditional_profile_compatible;
    for (auto& unit : result.design.units) {
        unit.vhdl_standard = vhdl_standard;
    }
    return result;
}

} // namespace fsim::frontend
