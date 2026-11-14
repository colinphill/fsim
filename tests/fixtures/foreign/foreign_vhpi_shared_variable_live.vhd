-- SPDX-License-Identifier: Apache-2.0
-- A shared variable that is not of a protected type (VHDL-93 only, IEEE
-- 1076-1993 4.3.1.3) is a variable: a VHPI program reads its current value
-- and updates it with a deposit (IEEE 1076-2019 22.3, 22.5.1, 22.5.2), and
-- the process that reads it next sees the deposited value.
entity top is
end entity;

architecture t of top is
  shared variable sv : integer := 5;
  shared variable sw : bit_vector(3 downto 0) := "1010";
  signal obs : integer := 0;
begin
  p : process is
  begin
    wait for 2 ns;
    obs <= sv;
    sv := sv + 1;
    wait for 1 ns;
    obs <= sv;
    wait;
  end process;
end architecture;
