// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_selection_vhdl_runtime_rejections()
{
    const auto invalid_vhdl_runtime_loop =
        fsim::frontend::parse_text(
            "invalid_vhdl_runtime_loop.vhd",
            R"(
entity invalid_vhdl_runtime_loop is
  port (condition : in std_logic);
end entity;
architecture rtl of invalid_vhdl_runtime_loop is
begin
  execute: process(condition)
  begin
    while condition loop
      null;
    end loop;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_runtime_loop.ok());
    const auto rejected_vhdl_runtime_loop =
        fsim::elaboration::elaborate(
            invalid_vhdl_runtime_loop.design,
            "vhdl:work.invalid_vhdl_runtime_loop(rtl)");
    assert(!rejected_vhdl_runtime_loop.ok());
    assert(
        has_diagnostic(
            rejected_vhdl_runtime_loop,
            "FSIM-ELAB-077"));

    const auto invalid_vhdl_condition =
        fsim::frontend::parse_text(
            "invalid_condition.vhd",
            R"(
entity invalid_condition is
  port (gate : in std_logic);
end entity;
architecture rtl of invalid_condition is
begin
  invalid: process(gate)
  begin
    if gate then
      null;
    end if;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_condition.ok());
    const auto rejected_vhdl_condition =
        fsim::elaboration::elaborate(
            invalid_vhdl_condition.design,
            "vhdl:work.invalid_condition(rtl)");
    assert(!rejected_vhdl_condition.ok());
    assert(has_diagnostic(
        rejected_vhdl_condition, "FSIM-ELAB-048"));
}

} // namespace fsim::tests::elaboration
