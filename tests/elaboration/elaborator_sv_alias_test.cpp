// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include "fsim/frontend/parser.hpp"

#include <cassert>

namespace fsim::tests::elaboration {

void test_systemverilog_aliases()
{
    const auto parsed = fsim::frontend::parse_text(
        "systemverilog-alias.sv",
        R"(
module systemverilog_alias;
  wire [7:0] first;
  wire [7:0] second;
  wire [7:0] third;
  wire [7:0] upper;
  wire [7:0] lower;
  wire [3:0] shuffled;
  alias first = second;
  alias second = third;
  alias upper[7:4] = lower[3:0];
  alias {upper[1:0], upper[3:2]} = shuffled;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "sv:work.systemverilog_alias");
    assert(elaborated.ok());
    const auto first = elaborated.design->find_signal(
        "systemverilog_alias.first");
    const auto second = elaborated.design->find_signal(
        "systemverilog_alias.second");
    const auto third = elaborated.design->find_signal(
        "systemverilog_alias.third");
    assert(first && second && third);
    assert(first == second && second == third);
    assert(elaborated.design->signals().size() == 4);
    const auto upper = elaborated.design->find_signal(
        "systemverilog_alias.upper");
    const auto lower = elaborated.design->find_signal(
        "systemverilog_alias.lower");
    const auto shuffled = elaborated.design->find_signal(
        "systemverilog_alias.shuffled");
    assert(upper && lower && shuffled);
    std::vector<fsim::runtime::simir::Process> aliases;
    for (const auto& process : elaborated.design->processes()) {
        if (process.switch_bidirectional) {
            aliases.push_back(process);
        }
    }
    assert(aliases.size() == 3);
    assert(aliases[0].switch_source == upper);
    assert(aliases[0].switch_target == lower);
    assert(aliases[0].switch_source_offset == 4);
    assert(aliases[0].switch_target_offset == 0);
    assert(aliases[0].switch_width == 4);
    assert(aliases[1].switch_source == upper);
    assert(aliases[1].switch_target == shuffled);
    assert(aliases[1].switch_source_offset == 2);
    assert(aliases[1].switch_target_offset == 0);
    assert(aliases[1].switch_width == 2);
    assert(aliases[2].switch_source == upper);
    assert(aliases[2].switch_target == shuffled);
    assert(aliases[2].switch_source_offset == 0);
    assert(aliases[2].switch_target_offset == 2);
    assert(aliases[2].switch_width == 2);

    const auto invalid = fsim::frontend::parse_text(
        "systemverilog-alias-invalid.sv",
        R"(
module systemverilog_alias_mismatch;
  wire [7:0] wide;
  wire [3:0] narrow;
  alias wide = narrow;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid.ok());
    const auto mismatch = fsim::elaboration::elaborate(
        invalid.design, "sv:work.systemverilog_alias_mismatch");
    assert(!mismatch.ok());
    assert(has_diagnostic(mismatch, "FSIM-ELAB-SVALIAS-003"));

    const auto variable = fsim::frontend::parse_text(
        "systemverilog-alias-variable.sv",
        R"(
module systemverilog_alias_variable;
  logic [7:0] left;
  logic [7:0] right;
  alias left = right;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(variable.ok());
    const auto invalid_variable = fsim::elaboration::elaborate(
        variable.design, "sv:work.systemverilog_alias_variable");
    assert(!invalid_variable.ok());
    assert(has_diagnostic(
        invalid_variable, "FSIM-ELAB-SVALIAS-001"));
}

} // namespace fsim::tests::elaboration
