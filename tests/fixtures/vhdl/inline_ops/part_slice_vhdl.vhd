-- SPDX-License-Identifier: Apache-2.0
--
-- Signal assignments to slices with variable bounds (IEEE 1076-2019
-- 10.5.2.1: the target is the slice named by the range at the time of the
-- assignment, and an element of the waveform goes to each element of the
-- slice), of a std_logic_vector signal, which the v4 kernel queues
-- inline (no generic WriteUpdateDynamicPartSlice), 'U' and weak values
-- included. s starts as sixteen '0'; when trig becomes '1' at 1 ns, the
-- process sensitive to it (compiled, unlike a process that waits for time)
-- assigns, with n = 4 (a signal, so the bounds are not static):
--   i = n:     s(7 downto 4)   <= "1HZU"   s(7)='1', s(6)='H', s(5)='Z', s(4)='U'
--   i = n + 6: s(11 downto 10) <= "WL"     s(11)='W', s(10)='L'
-- The slices do not overlap, so both apply in the same delta. The other
-- elements keep '0': s(15 downto 0) = 0000 WL00 1HZU 0000.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal s : std_logic_vector(15 downto 0) := (others => '0');
  signal n : integer := 4;
  signal trig : std_logic := '0';
begin
  trig <= '1' after 1 ns;

  writer : process (trig)
    variable i : integer;
  begin
    if trig = '1' then
      i := n;
      s(i + 3 downto i) <= "1HZU";
      i := n + 6;
      s(i + 1 downto i) <= "WL";
    end if;
  end process;

  reporter : process
  begin
    wait for 2 ns;
    report "wide dps=" & to_string(s) & ";";
    wait;
  end process;
end architecture;
