-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.2/12.5: "101" is a STRING and a BIT_VECTOR literal; both overloads fit.
-- Expected: FSIM-VHDL-TYPE-002.
-- fsim-standard: 2008
entity t03 is
end entity;
architecture a of t03 is
  procedure p(s : string) is begin end procedure;
  procedure p(s : bit_vector) is begin end procedure;
begin
  process
  begin
    p("101");
    wait;
  end process;
end architecture;
