-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL side of concat_logic9.sv: a top level that drives an 80 element
-- std_logic_vector holding all nine values, "UX01ZWLH-" repeated (element
-- k from the left is the (k mod 9)th character), into the SystemVerilog
-- module svm.
library ieee;
use ieee.std_logic_1164.all;

entity vtop is
end entity;

architecture a of vtop is
  component svm
    port (i : in std_logic_vector(79 downto 0));
  end component;
  signal s : std_logic_vector(79 downto 0) := "UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH";
begin
  u : svm port map (i => s);
end architecture;
