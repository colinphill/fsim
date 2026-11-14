-- SPDX-License-Identifier: Apache-2.0
--
-- Conversion functions on an inout std_logic port whose actual has another
-- driver (IEEE 1076-2019 4.6, 6.5.7.1, 14.7.3.2, 14.7.3.3):
-- `inv(p) => inv(w)`. inv of the port's driving value is one source of w,
-- resolved with the top's driver by std_logic_1164's resolution function;
-- the port's effective value is inv of the resolved value of w, and the
-- conversion of the actual is no source of the port (a port that counted it
-- among its own sources would resolve its driver's value with its own
-- effective value).
--   inv maps '0' to '1', '1' to '0' and every other value to itself.
--   w (default 'U'): the top's driver (starting at 'U', 'Z' at once, '1' at
--     2 ns, '0' at 4 ns) and inv of the port's driving value (the leaf's
--     driver: starting at the port's default 'U', '1' at 1 ns, 'Z' at 3 ns):
--     0.5 ns: resolved('Z', inv('U')) = 'U' (U wins),  p = inv('U') = 'U'
--     1.5 ns: resolved('Z', inv('1')) = '0',           p = inv('0') = '1'
--     2.5 ns: resolved('1', '0') = 'X',                p = inv('X') = 'X'
--     3.5 ns: resolved('1', inv('Z')) = '1',           p = inv('1') = '0'
--     4.5 ns: resolved('0', 'Z') = '0',                p = inv('0') = '1'
library ieee;
use ieee.std_logic_1164.all;

package conv_std_logic_shared_pkg is
  function inv (x : std_logic) return std_logic;
end package;

package body conv_std_logic_shared_pkg is
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
use work.conv_std_logic_shared_pkg.all;

entity conv_std_logic_shared_leaf is
  port (p : inout std_logic := 'U');
end entity;

architecture rtl of conv_std_logic_shared_leaf is
begin
  p <= '1' after 1 ns, 'Z' after 3 ns;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.conv_std_logic_shared_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : std_logic;
  alias pv is << signal .top.u.p : std_logic >>;
begin
  u : entity work.conv_std_logic_shared_leaf port map (inv(p) => inv(w));
  w <= 'Z', '1' after 2 ns, '0' after 4 ns;

  events : process (w)
  begin
    report "wide event w=" & std_logic'image(w) & " at "
      & integer'image(now / 1 ns);
  end process;

  check : process
  begin
    wait for 500 ps;
    for step in 0 to 4 loop
      report "wide w=" & std_logic'image(w) & " p=" & std_logic'image(pv)
        & " at " & integer'image(step) & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
