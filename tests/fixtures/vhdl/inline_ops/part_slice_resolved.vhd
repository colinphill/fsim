-- SPDX-License-Identifier: Apache-2.0
--
-- A signal assignment to a slice with variable bounds (IEEE 1076-2019
-- 10.5.2.1) of a resolved std_logic_vector signal that a second process
-- also drives, which the v4 kernel resolves into the signal's next block
-- inline (no generic WriteUpdateDynamicPartSlice). The longest static prefix
-- of the target s(i + 3 downto i) is s, so the writer has a driver for each
-- of the eight elements (14.7.2), whose initial contents are the default
-- value 'U' (14.7.2); the other process drives 'Z' on every element.
-- When trig becomes '1' at 1 ns the writer (sensitive to trig, so compiled)
-- assigns, with n = 2 (a signal, so the bounds are not static):
--   s(5 downto 2) <= "1HZU"   s(5)='1', s(4)='H', s(3)='Z', s(2)='U'
-- The resolved value of an element is resolved(writer, 'Z') by the table of
-- std_logic_1164: ('U','Z') = 'U', ('1','Z') = '1', ('H','Z') = 'H',
-- ('Z','Z') = 'Z'. Elements 7, 6, 1 and 0 keep the writer's 'U' driver
-- value, so the signal reads, from element 7 down to 0: U U 1 H Z U U U.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal s : std_logic_vector(7 downto 0);
  signal n : integer := 2;
  signal trig : std_logic := '0';
begin
  trig <= '1' after 1 ns;

  other : process
  begin
    s <= (others => 'Z');
    wait;
  end process;

  writer : process (trig)
    variable i : integer;
  begin
    if trig = '1' then
      i := n;
      s(i + 3 downto i) <= "1HZU";
    end if;
  end process;

  reporter : process
  begin
    wait for 2 ns;
    report "wide res=" & to_string(s) & ";";
    wait;
  end process;
end architecture;
