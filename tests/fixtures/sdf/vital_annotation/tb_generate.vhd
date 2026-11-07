-- SPDX-License-Identifier: Apache-2.0
--
-- Buffers in a FOR generate (g(0), g(1)) and an IF generate (c), with the
-- stimulus of tb_path. generate_30.sdf annotates tpd_a_y (IEEE 1076.4-2000
-- 5.2.4): g[0:1].u (6, 7) ns for both expansions, then g[1].u INCREMENT
-- (1, 1) ns, so g(0) has (6, 7) ns, g(1) has (7, 8) ns, and c.v (8, 9) ns.
-- The default tipd_a = (0.5, 0.7) ns: a_ipd falls at 0.7 ns, rises at 10.5 ns
-- and falls at 30.7 ns, and each output follows by its tpd_a_y:
--   y0 falls at 0.7 + 7 = 7.7 ns, rises at 10.5 + 6 = 16.5 ns, falls at 37.7 ns;
--   y1 falls at 0.7 + 8 = 8.7 ns, rises at 10.5 + 7 = 17.5 ns, falls at 38.7 ns;
--   y2 falls at 0.7 + 9 = 9.7 ns, rises at 10.5 + 8 = 18.5 ns, falls at 39.7 ns.
library ieee;
use ieee.std_logic_1164.all;

entity tb_generate is end entity;

architecture t of tb_generate is
  component vbuf port (a : in std_logic; y : out std_logic); end component;
  signal a : std_logic := '0';
  signal y : std_logic_vector(0 to 2);
begin
  g : for i in 0 to 1 generate
    u : vbuf port map (a, y(i));
  end generate;

  c : if true generate
    v : vbuf port map (a, y(2));
  end generate;

  stim : process
  begin
    wait for 10 ns;
    a <= '1';
    wait for 20 ns;
    a <= '0';
    wait;
  end process;

  m : for i in 0 to 2 generate
    mon : process (y(i))
    begin
      if y(i)'event then
        report "TRACE y" & integer'image(i) & "=" & std_logic'image(y(i))
          & " @" & time'image(now);
      end if;
    end process;
  end generate;
end architecture;
