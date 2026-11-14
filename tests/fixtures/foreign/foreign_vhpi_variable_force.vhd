-- SPDX-License-Identifier: Apache-2.0
-- Forcing, releasing and watching variables through VHPI (IEEE 1076-2019
-- 22.5.2, 21.3.2.2-4). A forced variable is not updated by a variable
-- assignment or by association with an out or inout formal, a deposit on it
-- has no effect, and a subelement can be forced alone; a release leaves the
-- forced value until the next assignment. A value-change callback on a
-- variable runs at each change by assignment, association or
-- vhpi_put_value.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  signal clk : bit := '0';
begin
  clk <= not clk after 1 ns when now < 8 ns;
  p : process (clk) is
    variable n : integer := 0;
    variable v : std_logic_vector(3 downto 0) := "0000";
    variable t : integer := 0;
    procedure bump (variable x : inout integer) is
    begin
      x := x + 1;
    end procedure;
  begin
    if clk = '1' then
      n := n + 1;
      v := v(2 downto 0) & '1';
      bump(t);
    end if;
  end process;
end architecture;
