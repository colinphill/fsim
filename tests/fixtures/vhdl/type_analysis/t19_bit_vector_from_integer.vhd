-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.6: an integer type is not closely related to an array type.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t19 is
end entity;
architecture a of t19 is
begin
  process
    variable i : integer := 1;
    variable v : bit_vector(1 to 4);
  begin
    v := bit_vector(i);
    wait;
  end process;
end architecture;
