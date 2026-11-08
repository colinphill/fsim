-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL side of four_plane.sv: a top level whose std_logic_vector signal
-- reads the SystemVerilog module's output.
library ieee;
use ieee.std_logic_1164.all;

entity vtop is
end entity;

architecture a of vtop is
  component svm
    port (q : out std_logic_vector(7 downto 0));
  end component;
  signal s : std_logic_vector(7 downto 0);
begin
  u : svm port map (q => s);
end architecture;
