-- SPDX-License-Identifier: Apache-2.0
-- The port map of a binding indication with a conversion function in the
-- actual part (`r => inv(c)`, the entity port reads the converted local
-- port) and in the formal part (`inv(s) => d`, the converted entity port
-- drives the local port), IEEE 1076-2019 7.3.2.1, 6.5.7.1, 14.5.4. Each
-- association element has the entity port as vhpiFormal; the actual part's
-- conversion stands for its part as vhpiActual (19.12.2.2, class
-- convFunc), a local port of the component is a portDecl.
package cv is
  function inv (x : bit) return bit;
end package;

package body cv is
  function inv (x : bit) return bit is
  begin
    return not x;
  end function;
end package body;

use work.cv.all;
entity ent is
  port (r : in bit; s : out bit);
end entity;

architecture e of ent is
begin
  s <= r;
end architecture;

use work.cv.all;
entity top is
end entity;

architecture t of top is
  component cmp is
    port (c : in bit; d : out bit);
  end component;
  for u : cmp use entity work.ent port map (r => inv(c), inv(s) => d);
  signal sc, sd : bit;
begin
  u : cmp port map (c => sc, d => sd);
  drive : process
  begin
    sc <= '0';
    wait for 10 ns;
    sc <= '1';
    wait for 10 ns;
    sc <= '0';
    wait;
  end process;
end architecture;
