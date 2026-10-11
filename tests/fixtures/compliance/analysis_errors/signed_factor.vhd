-- fsim-expect: FSIM-VHDL-PARSE-297 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 9.1: a sign may only begin a simple expression.
entity signed_factor is
end entity;

architecture a of signed_factor is
begin
  process
    variable n : integer := 6;
  begin
    n := n / -2;
    wait;
  end process;
end architecture;
