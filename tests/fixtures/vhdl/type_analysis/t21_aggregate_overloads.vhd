-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.3.1 and 12.5: the aggregate ('0','1') fits both BIT_VECTOR and STRING formals; the context leaves two interpretations.
-- Expected: FSIM-VHDL-TYPE-002.
-- fsim-standard: 2008
entity t21 is
end entity;
architecture a of t21 is
  procedure p(x : bit_vector) is begin end procedure;
  procedure p(x : string) is begin end procedure;
begin
  process
  begin
    p(('0', '1'));
    wait;
  end process;
end architecture;
