-- SPDX-License-Identifier: Apache-2.0
--
-- Guarded concurrent assignments (IEEE 1076-2019 11.6). The process
-- equivalent to a guarded assignment whose target is not a guarded signal
-- is `if GUARD then signal_transform end if;`: a false guard leaves the
-- driver as it is, whatever the target's type (std_logic u and the slice
-- uv(2 downto 1) here keep their last values). Only a guarded target has
-- the else branch, its disconnection statement: the bus b's one driver is
-- disconnected (a null transaction after 0 ns, 7.4) and the resolution of
-- no sources is 'Z' (14.7.3.2, std_logic_1164 resolved of an empty array).
-- uv(3) and uv(0) have no source and keep the default '1'. A single source
-- of a resolved signal gives its own value, '-' too.
--   report 0 (g true):          u '0' uv "1101" b '0'
--   report 1 (g false):         u '0' uv "1101" b 'Z'
--   report 2 (v, vv change):    u '0' uv "1101" b 'Z'
--   report 3 (g true):          u '1' uv "1011" b '1'
--   report 4 (v '-'):           u '-' uv "1011" b '-'
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal u : std_logic := '1';
  signal uv : std_logic_vector(3 downto 0) := "1111";
  signal b : std_logic bus;
  signal g : boolean := true;
  signal v : std_logic := '0';
  signal vv : std_logic_vector(3 downto 0) := "0101";
begin
  blk : block (g)
  begin
    u <= guarded v;
    uv(2 downto 1) <= guarded vv(2 downto 1);
    b <= guarded v;
  end block;

  stim : process
    procedure show (n : integer) is
    begin
      report "wide guard " & integer'image(n) & " u=" & std_logic'image(u)
        & " uv=" & to_string(uv) & " b=" & std_logic'image(b) & ";";
    end procedure;
  begin
    wait for 1 ns;
    show(0);
    g <= false;
    wait for 1 ns;
    show(1);
    v <= '1';
    vv <= "1010";
    wait for 1 ns;
    show(2);
    g <= true;
    wait for 1 ns;
    show(3);
    v <= '-';
    wait for 1 ns;
    show(4);
    wait;
  end process;
end architecture;
