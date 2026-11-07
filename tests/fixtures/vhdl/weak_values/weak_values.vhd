-- SPDX-License-Identifier: Apache-2.0
--
-- All nine std_logic values in registers of the v4 kernel: 'H', 'L', 'W' and
-- '-' are computed, stored, read back, compared and combined on narrow
-- (8-element) and wide (100-element) std_logic_vectors, on a pulled-up
-- resolved signal and in a conditional assignment. The expected reports
-- (weak_values.expected) are derived from the IEEE 1164 tables:
--
--   and  : 'H' acts as '1', 'L' as '0'; 'W', '-', 'X', 'Z' with '1' give 'X';
--          any operand '0' or 'L' gives '0'.
--   not  : H -> 0, L -> 1, W -> X, - -> X, 0 -> 1, 1 -> 0, X -> X, Z -> X.
--   xor  : H as 1, L as 0; W, -, X, Z with either operand give 'X'.
--   to_x01: H -> 1, L -> 0, W -> X, - -> X, 0, 1 and X as they are, Z -> X.
--   "="  : the predefined enumeration compare, so 'H' /= '1' and 'L' /= '0'.
--   resolved: ('Z','H') -> 'H'; ('0','H') -> '0'.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal en   : std_logic := '0';
  signal d    : std_logic := '1';
  signal x    : std_logic;
  signal y    : std_logic;
  signal sda  : std_logic;
  signal v8   : std_logic_vector(7 downto 0);
  signal and8 : std_logic_vector(7 downto 0);
  signal not8 : std_logic_vector(7 downto 0);
  signal xor8 : std_logic_vector(7 downto 0);
  signal x01  : std_logic_vector(7 downto 0);
  signal v100 : std_logic_vector(99 downto 0);
  signal and100 : std_logic_vector(99 downto 0);
  signal not100 : std_logic_vector(99 downto 0);
  signal x100 : std_logic_vector(99 downto 0);
  signal copy100 : std_logic_vector(99 downto 0);
begin
  -- 'H' as a constant and as the other branch of a conditional value.
  x <= 'H' when en = '0' else d;
  y <= '-' when en = '0' else d;
  -- Open drain with a weak pull-up: two drivers of one resolved signal.
  sda <= 'Z' when en = '0' else '0';
  sda <= 'H';

  v8 <= "HL-W01XZ";
  and8 <= v8 and "11111111";
  not8 <= not v8;
  xor8 <= v8 xor "10101010";
  x01 <= to_x01(v8);

  v100 <= (99 downto 90 => 'H', 89 downto 80 => 'L', 79 downto 70 => '-',
           69 downto 60 => 'W', others => '1');
  and100 <= v100 and (99 downto 0 => '1');
  not100 <= not v100;
  x100 <= to_x01(v100);
  copy100 <= v100;

  stim : process
    variable vv : std_logic_vector(7 downto 0) := (others => 'Z');
  begin
    wait for 1 ns;
    report "weak x=" & std_logic'image(x) & ";";
    report "weak x=1:" & boolean'image(x = '1') & ";";
    report "weak x=H:" & boolean'image(x = 'H') & ";";
    report "weak to_x01(x)=" & std_logic'image(to_x01(x)) & ";";
    report "weak y=" & std_logic'image(y) & ";";
    report "weak to_x01(y)=" & std_logic'image(to_x01(y)) & ";";
    report "weak sda=" & std_logic'image(sda) & ";";
    report "weak v8=" & to_string(v8) & ";";
    report "weak and8=" & to_string(and8) & ";";
    report "weak not8=" & to_string(not8) & ";";
    report "weak xor8=" & to_string(xor8) & ";";
    report "weak x01=" & to_string(x01) & ";";
    report "weak v8=lit:" & boolean'image(v8 = "HL-W01XZ") & ";";
    report "weak v8=strong:" & boolean'image(v8 = "10-W01XZ") & ";";
    report "weak v100=" & to_string(v100) & ";";
    report "weak and100=" & to_string(and100) & ";";
    report "weak not100=" & to_string(not100) & ";";
    report "weak x100=" & to_string(x100) & ";";
    report "weak v100=copy:" & boolean'image(v100 = copy100) & ";";
    report "weak v100=x100:" & boolean'image(v100 = x100) & ";";
    vv(3) := 'H';
    vv(4) := 'L';
    report "weak vv=" & to_string(vv) & ";";
    en <= '1';
    wait for 1 ns;
    report "weak x=" & std_logic'image(x) & ";";
    report "weak x=1:" & boolean'image(x = '1') & ";";
    report "weak y=" & std_logic'image(y) & ";";
    report "weak sda=" & std_logic'image(sda) & ";";
    std.env.finish;
  end process;
end architecture;
