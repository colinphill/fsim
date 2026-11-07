-- SPDX-License-Identifier: Apache-2.0
--
-- The only weak values of this design come from std_ulogic'val(n) with n a
-- signal, so nothing folds: 'val of position n is the n-th literal of the
-- declaration of STD_ULOGIC in IEEE 1164 (IEEE 1076-2019 5.2.2.1, 16.2.2),
--   0 'U', 1 'X', 2 '0', 3 '1', 4 'Z', 5 'W', 6 'L', 7 'H', 8 '-',
-- and a position outside 0 to 8 is an error. 'image of each is the literal
-- with its quotes.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal n : integer := 0;
  signal s : std_ulogic;
begin
  s <= std_ulogic'val(n);

  stim : process
  begin
    for i in 0 to 8 loop
      n <= i;
      wait for 1 ns;
      report "weak val(" & integer'image(i) & ")=" & std_ulogic'image(s) & ";";
    end loop;
    std.env.finish;
  end process;
end architecture;
