-- SPDX-License-Identifier: Apache-2.0
-- Both conversions in a configuration declaration: sd = sc.
-- Binding indication port map conversions (IEEE 1076-2019 7.3.2.2, 6.5.7.1,
-- 6.5.7.3, 14.5.4): the entity's ports are associated with the implied
-- component block's ports; a conversion function in the actual part reads
-- the component port, and one in the formal part drives the component port.
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
  check : process
  begin
    wait for 5 ns;
    report "bc4 5 ns sd=" & integer'image(bit'pos(sd));
    wait for 10 ns;
    report "bc4 15 ns sd=" & integer'image(bit'pos(sd));
    wait for 10 ns;
    report "bc4 25 ns sd=" & integer'image(bit'pos(sd));
    wait;
  end process;
end architecture;

-- The conversion function is named in the configuration declaration: its
-- context clause makes it visible (IEEE 1076-2019 3.4.1, 13.1).
use work.cv.all;
configuration cfg of top is
  for t
    for u : cmp
      use entity work.ent port map (r => inv(c), inv(s) => d);
    end for;
  end for;
end configuration;
