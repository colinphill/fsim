-- fsim: top=d8_array_range_segment(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 9.3.3.3: an element association whose choices are discrete
-- ranges may give an expression of the aggregate's own array type, whose
-- elements fill the range. A partial association of a formal by a slice
-- range (6.5.7.1) is checked as an association, not as such an aggregate.
library ieee;
use ieee.std_logic_1164.all;

entity d8_array_range_segment is
end entity;

architecture test of d8_array_range_segment is
  constant c_a : std_logic_vector(11 downto 0) := x"FAE";
  procedure proc (
    signal src : in std_logic_vector(3 downto 0);
    signal dst : out std_logic_vector(3 downto 0)) is
  begin
    dst <= src;
  end procedure;
  signal a : std_logic_vector(3 downto 0) := "1010";
  signal b : std_logic_vector(3 downto 0);
begin
  process
    variable s : std_logic_vector(15 downto 0);
  begin
    s := (15 downto 12 => c_a(11), 11 downto 0 => c_a);
    assert s = x"FFAE" severity failure;
    s := (15 downto 12 => '0', 11 downto 0 => c_a);
    assert s = x"0FAE" severity failure;
    proc(src => a, dst(3 downto 0) => b);
    wait for 1 ns;
    assert b = "1010" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
