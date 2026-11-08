-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal w128 : std_logic_vector(127 downto 0);
  shared variable sv : std_logic_vector(127 downto 0);
  shared variable sw : std_logic_vector(127 downto 0) := (others => '0');
  shared variable sb : bit_vector(127 downto 0);

  function image(v : std_logic_vector) return string is
    constant chars : string(1 to 9) := "UX01ZWLH-";
    variable r : string(1 to v'length);
    variable n : integer := 1;
  begin
    for k in v'range loop
      r(n) := chars(std_logic'pos(v(k)) + 1);
      n := n + 1;
    end loop;
    return r;
  end function;
begin
  w128 <= (127 downto 96 => 'H', 95 downto 64 => 'L', 63 downto 32 => 'W', 31 downto 0 => '-');

  stim : process
  begin
    wait for 1 ns;
    sv := w128;
    report "shared sv=" & image(sv(67 downto 60)) & ";";
    sw := w128;
    sw(65 downto 62) := "1100";
    report "shared sw=" & image(sw(67 downto 60)) & ";";
    report "shared swlow=" & image(sw(33 downto 28)) & ";";
    sb := (others => '1');
    sb(66 downto 61) := "000000";
    report "shared sb=" & integer'image(boolean'pos(sb(67) = '1')) & integer'image(boolean'pos(sb(66) = '1'))
      & integer'image(boolean'pos(sb(61) = '1')) & integer'image(boolean'pos(sb(60) = '1')) & ";";
    sv := (others => 'U');
    sv(100) := 'X';
    report "shared svu=" & image(sv(101 downto 99)) & ";";
    wait;
  end process;
end architecture;
