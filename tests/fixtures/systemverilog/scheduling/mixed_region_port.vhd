-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL half of mixed_region_port.sv: the SystemVerilog leaf drives
-- region through a wider component port (zero-extended), and the watch
-- process reports every value region takes.
library ieee;
use ieee.std_logic_1164.all;
use std.textio.all;

entity mixed_region_mid is
  port (seen : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of mixed_region_mid is
  component mixed_region_leaf is
    port (result : out std_logic_vector(3 downto 0));
  end component;
  signal region : std_logic_vector(3 downto 0);
begin
  leaf : mixed_region_leaf port map (result => region);
  seen <= region;
  watch : process (region)
    variable text : line;
  begin
    write(text, string'("MIX vhdl region=") & to_string(region)
      & string'(" at ") & time'image(now));
    writeline(output, text);
  end process;
end architecture;
