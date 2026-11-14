-- SPDX-License-Identifier: Apache-2.0
--
-- Conversion functions on an inout std_logic port (IEEE 1076-2019 4.6,
-- 6.5.7.1, 14.7.3.2, 14.7.3.3, 14.7.5.2): `inv(p) => inv(w)`. The port's
-- driving value is the resolution function over its own drivers; inv of it
-- is the one source of w. The port's effective value is inv of w's, and the
-- conversion of the actual is no source of the port: a port that counted it
-- among its own sources would resolve its driver's value with its own
-- effective value and leave w at 'U'.
--   inv maps '0' to '1', '1' to '0' and every other value to itself.
--   w (default 'U'): its one source is inv of the port's driving value. The
--     port's driver starts at the port's default 'U' (14.7.5.2), so w starts
--     at inv('U') = 'U' and the port at inv('U') = 'U'.
--   The leaf drives its port with '1' at 1 ns and 'Z' at 3 ns:
--     0.5 ns: w = 'U', p = 'U'
--     1.5 ns: w = inv('1') = '0', p = inv('0') = '1'
--     2.5 ns: w = '0', p = '1'
--     3.5 ns: w = inv('Z') = 'Z', p = inv('Z') = 'Z'
--   w changes once, in the cycle the port's driver does: at 1 ns and 3 ns.
library ieee;
use ieee.std_logic_1164.all;

package conv_std_logic_pkg is
  function inv (x : std_logic) return std_logic;
end package;

package body conv_std_logic_pkg is
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
use work.conv_std_logic_pkg.all;

entity conv_std_logic_leaf is
  port (p : inout std_logic := 'U');
end entity;

architecture rtl of conv_std_logic_leaf is
begin
  p <= '1' after 1 ns, 'Z' after 3 ns;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.conv_std_logic_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : std_logic;
  alias pv is << signal .top.u.p : std_logic >>;
begin
  u : entity work.conv_std_logic_leaf port map (inv(p) => inv(w));

  events : process (w)
  begin
    report "wide event w=" & std_logic'image(w) & " at "
      & integer'image(now / 1 ns);
  end process;

  check : process
  begin
    wait for 500 ps;
    for step in 0 to 3 loop
      report "wide w=" & std_logic'image(w) & " p=" & std_logic'image(pv)
        & " at " & integer'image(step) & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
