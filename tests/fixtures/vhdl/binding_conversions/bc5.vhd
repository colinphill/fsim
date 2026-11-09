-- SPDX-License-Identifier: Apache-2.0
-- Both conversions, with local ports whose actuals are not signal names: the
-- implied block gets a signal for each such local port (IEEE 1076-2019
-- 14.5.4, 6.5.6.3). r = inv(not sc) = sc, s = r, sv(1) = inv(s) = not sc.
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
  for u : cmp use entity work.ent port map (r => inv(c), inv(s) => d);
  signal sc : bit;
  signal sv : bit_vector(1 downto 0);
begin
  u : cmp port map (c => not sc, d => sv(1));
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
    report "bc5 5 ns sd=" & integer'image(bit'pos(sv(1)));
    wait for 10 ns;
    report "bc5 15 ns sd=" & integer'image(bit'pos(sv(1)));
    wait for 10 ns;
    report "bc5 25 ns sd=" & integer'image(bit'pos(sv(1)));
    wait;
  end process;
end architecture;

