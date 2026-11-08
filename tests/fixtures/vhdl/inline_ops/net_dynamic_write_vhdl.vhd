-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL side of net_dynamic_write.sv: drives 'Z' onto the net that the
-- SystemVerilog output port q also drives, and reports its resolved value at
-- 1 ns, 0000101000100000 (the SystemVerilog writes win over 'Z').
library ieee;
use ieee.std_logic_1164.all;

entity vtop is
end entity;

architecture a of vtop is
  component svm
    port (q : out std_logic_vector(15 downto 0));
  end component;
  signal bus16 : std_logic_vector(15 downto 0);
begin
  u : svm port map (q => bus16);
  bus16 <= (others => 'Z');
  process
  begin
    wait for 1 ns;
    report "inl nd " & to_string(bus16);
    wait;
  end process;
end architecture;
