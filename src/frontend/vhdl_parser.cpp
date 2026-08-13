// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

ParseResult parse_vhdl(SourceText source, const VhdlStandard vhdl_standard)
{
    auto result = VhdlParser(
        lex(std::move(source), Language::Vhdl2008, vhdl_standard),
        vhdl_standard)
                      .run();
    for (auto& unit : result.design.units) {
        unit.vhdl_standard = vhdl_standard;
    }
    return result;
}

}  // namespace fsim::frontend
