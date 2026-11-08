-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL side of sv_shift_logic9.sv: a top level that drives an 8 element
-- std_logic_vector holding "UX01ZWLH" into the SystemVerilog module svm.
library ieee;
use ieee.std_logic_1164.all;

entity vtop is
end entity;

architecture a of vtop is
  component svm
    port (i : in std_logic_vector(7 downto 0));
  end component;
  signal s : std_logic_vector(7 downto 0) := "UX01ZWLH";
begin
  u : svm port map (i => s);
end architecture;
