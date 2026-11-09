-- SPDX-License-Identifier: Apache-2.0
-- A shared variable that is not of a protected type exists only in VHDL-87
-- to 93 designs (IEEE 1076-1993 4.3.1.3); its value is readable like that of
-- any variable (IEEE 1076-2019 22.3), here the value of its initial
-- expression at the start of the initialization phase (vhpiCbStartOfInitialization,
-- 21.3.7.5), before any process has run and assigned it (14.7.5.2).
entity top is
end entity;

architecture t of top is
  shared variable sv : integer := 5;
  shared variable sw : bit_vector(3 downto 0) := "1010";
begin
  p : process is
  begin
    sv := sv + 1;
    wait;
  end process;
end architecture;
