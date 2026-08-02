// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_matching_statements() {
  using fsim::runtime::simir::Binary;
  using fsim::runtime::simir::BinaryOperator;

  const auto positive = fsim::frontend::parse_text(
      "vhdl_matching.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_matching is
  port (selector : in std_logic_vector(3 downto 0);
        observed : out integer);
end entity;
architecture rtl of vhdl_matching is
begin
  process(selector)
    variable value : integer := 0;
  begin
    case? selector is
      when "1001" => value := 1;
      when others => value := 0;
    end case?;
    with selector select?
      value := value + 1 when "10--", value when others;
    observed <= value + 1 when selector ?= "1---" else value;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(positive.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      positive.design, "vhdl:work.vhdl_matching(rtl)");
  assert(elaborated.ok());
  assert(elaborated.design);
  const auto& operations =
      elaborated.design->processes().front().operations;
  assert(std::ranges::count_if(
             operations,
             [](const auto& operation) {
               const auto* binary =
                   fsim::runtime::simir::operation_get_if<Binary>(
                       &operation);
               return binary != nullptr
                   && binary->operation
                       == BinaryOperator::vhdl_match_equal;
             })
         == 3);

  const auto overlap = fsim::frontend::parse_text(
      "vhdl_matching_overlap.vhd",
      R"(
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_matching_overlap is
  port (selector : in std_logic_vector(1 downto 0));
end entity;
architecture rtl of vhdl_matching_overlap is
begin
  process(selector) begin
    case? selector is
      when "1-" => null;
      when "-1" => null;
      when others => null;
    end case?;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(overlap.ok());
  const auto rejected_overlap = fsim::elaboration::elaborate(
      overlap.design, "vhdl:work.vhdl_matching_overlap(rtl)");
  assert(!rejected_overlap.ok());
  assert(has_diagnostic(
      rejected_overlap, "FSIM-ELAB-VHDLMATCH-003"));

  const auto integer_selector = fsim::frontend::parse_text(
      "vhdl_matching_integer.vhd",
      R"(
entity vhdl_matching_integer is end entity;
architecture rtl of vhdl_matching_integer is begin
  process begin
    case? 1 is when 1 => null; when others => null; end case?;
    if 1 ?= 1 then null; end if;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(integer_selector.ok());
  const auto rejected_integer = fsim::elaboration::elaborate(
      integer_selector.design,
      "vhdl:work.vhdl_matching_integer(rtl)");
  assert(!rejected_integer.ok());
  assert(has_diagnostic(
      rejected_integer, "FSIM-ELAB-VHDLMATCH-001"));
  assert(has_diagnostic(
      rejected_integer, "FSIM-ELAB-VHDLMATCH-004"));
}

}  // namespace fsim::tests::elaboration
