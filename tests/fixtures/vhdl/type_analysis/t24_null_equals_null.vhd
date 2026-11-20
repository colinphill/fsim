-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.6/9.3.2: the operand of a type conversion is typed alone, so null gets no type.
-- Expected: FSIM-VHDL-TYPE-003.
-- fsim-standard: 2008
entity t24 is
end entity;
architecture a of t24 is
  type ptr is access integer;
begin
  process
    variable p : ptr;
  begin
    p := ptr(null);
    wait;
  end process;
end architecture;
