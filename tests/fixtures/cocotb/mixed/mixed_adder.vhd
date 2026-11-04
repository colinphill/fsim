-- SPDX-License-Identifier: Apache-2.0
-- The VHDL children of mixed_top.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed_adder is
  port (
    a : in  std_logic_vector(7 downto 0);
    b : in  std_logic_vector(7 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of mixed_adder is
  signal total : unsigned(7 downto 0);
begin
  total <= unsigned(a) + unsigned(b);
  y <= std_logic_vector(total);
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- A flop on the SystemVerilog clock, fed by a SystemVerilog flop.
entity mixed_flop is
  port (
    clk : in  std_logic;
    d   : in  std_logic_vector(7 downto 0);
    q   : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of mixed_flop is
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      q <= d;
    end if;
  end process;
end architecture;
