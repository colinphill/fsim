-- SPDX-License-Identifier: Apache-2.0
-- The VHDL half of the mixed-language hierarchy design: a VHDL instance
-- under SystemVerilog, a SystemVerilog instance under it, and a VHDL
-- instance under that.
library ieee;
use ieee.std_logic_1164.all;

entity mixed_vleaf is
  port (a : in std_logic);
end entity;

architecture rtl of mixed_vleaf is
  signal s : std_logic;
begin
  s <= a;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity mixed_mid is
  port (a : in std_logic; y : out std_logic);
end entity;

architecture rtl of mixed_mid is
  signal t : std_logic;
  component mixed_sleaf is
    port (a : in std_logic; y : out std_logic);
  end component;
begin
  t <= a;
  l : mixed_sleaf port map (a => t, y => y);
end architecture;
