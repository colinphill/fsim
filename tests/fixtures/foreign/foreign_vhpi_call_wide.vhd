-- SPDX-License-Identifier: Apache-2.0
-- A deposit into a 100-bit local of an outer activation of a recursive
-- procedure (IEEE 1076-2019 14.6 b, 22.5.2): the activations share the
-- registers of their frame, the newer ones holding the older one's value,
-- and the deposited value is the one the outer activation has when the
-- recursion returns to it.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture rtl of top is
begin
  main : process is
    variable k : integer := 3;
    variable acc : std_logic_vector(99 downto 0) := (others => '0');
    procedure down(n : integer) is
      variable v : std_logic_vector(99 downto 0) := (others => '0');
    begin
      v(n * 30) := '1';
      if n > 1 then
        down(n - 1);
      end if;
      acc := acc or v;
    end procedure;
  begin
    down(k);
    wait for 1 ns;
    wait;
  end process;
end architecture;
