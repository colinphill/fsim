-- SPDX-License-Identifier: Apache-2.0
--
-- A std_logic actual with three kinds of source (IEEE 1076-2019 4.6,
-- 6.5.7.1, 14.7.3.2, 14.7.3.3): an ordinary process of the top, the
-- conversion of the driving value of an inout port (`inv(p) => inv(w)`) and
-- the conversion of an out port (`inv(o) => w`). Resolved by std_logic_1164's
-- function; the inout port's effective value is inv of the resolved value
-- and no source of it.
--   inv maps '0' to '1', '1' to '0' and every other value to itself.
--   w (default 'U'): T, the top's driver ('Z' at once, '1' at 4 ns); A, inv of
--   the inout port's driving value ('U' at first, '1' at 1 ns, 'Z' at 6 ns);
--   B, inv of the out port's driving value ('U' at first, '0' at 2 ns, 'Z' at
--   5 ns):
--     0.5 ns: resolved('Z', inv('U'), inv('U')) = 'U',  p = inv('U') = 'U'
--     1.5 ns: resolved('Z', inv('1') = '0', 'U') = 'U', p = 'U'
--     2.5 ns: resolved('Z', '0', inv('0') = '1') = 'X', p = inv('X') = 'X'
--     3.5 ns: 'X', p = 'X'
--     4.5 ns: resolved('1', '0', '1') = 'X', p = 'X'
--     5.5 ns: resolved('1', '0', inv('Z') = 'Z') = 'X', p = 'X'
--     6.5 ns: resolved('1', inv('Z') = 'Z', 'Z') = '1', p = inv('1') = '0'
library ieee;
use ieee.std_logic_1164.all;

package conv_std_logic_mixed_pkg is
  function inv (x : std_logic) return std_logic;
end package;

package body conv_std_logic_mixed_pkg is
  function inv (x : std_logic) return std_logic is
  begin
    case x is
      when '0' => return '1';
      when '1' => return '0';
      when others => return x;
    end case;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.conv_std_logic_mixed_pkg.all;

entity conv_std_logic_mixed_inout is
  port (p : inout std_logic := 'U');
end entity;

architecture rtl of conv_std_logic_mixed_inout is
begin
  p <= '1' after 1 ns, 'Z' after 6 ns;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.conv_std_logic_mixed_pkg.all;

entity conv_std_logic_mixed_out is
  port (o : out std_logic := 'U');
end entity;

architecture rtl of conv_std_logic_mixed_out is
begin
  o <= '0' after 2 ns, 'Z' after 5 ns;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.conv_std_logic_mixed_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : std_logic;
  alias pv is << signal .top.u.p : std_logic >>;
begin
  u : entity work.conv_std_logic_mixed_inout port map (inv(p) => inv(w));
  v : entity work.conv_std_logic_mixed_out port map (inv(o) => w);
  w <= 'Z', '1' after 4 ns;

  events : process (w)
  begin
    report "wide event w=" & std_logic'image(w) & " at "
      & integer'image(now / 1 ns);
  end process;

  check : process
  begin
    wait for 500 ps;
    for step in 0 to 6 loop
      report "wide w=" & std_logic'image(w) & " p=" & std_logic'image(pv)
        & " at " & integer'image(step) & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
