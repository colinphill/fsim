-- SPDX-License-Identifier: Apache-2.0
--
-- A globally static aggregate as a generic actual: tpd_a_y => (r(i), f(i)) in
-- a FOR generate, where r and f are constant arrays of TIME and i is the
-- generate parameter (IEEE 1076-2019 14.4.2.1, 11.8). The actual has the
-- value of its own generate iteration, and the cell receives it.
-- r = (2 ns, 3 ns) and f = (4 ns, 5 ns). The default tipd_a = (0.5, 0.7) ns:
-- a_ipd falls at 0.7 ns, rises at 10.5 ns and falls at 30.7 ns, and each
-- output follows by its tpd_a_y:
--   y0 falls at 0.7 + 4 = 4.7 ns, rises at 10.5 + 2 = 12.5 ns, falls at
--   30.7 + 4 = 34.7 ns;
--   y1 falls at 0.7 + 5 = 5.7 ns, rises at 10.5 + 3 = 13.5 ns, falls at
--   30.7 + 5 = 35.7 ns.
library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;

entity tb_aggregate is end entity;

architecture t of tb_aggregate is
  type delay_array is array (0 to 1) of time;
  constant r : delay_array := (2 ns, 3 ns);
  constant f : delay_array := (4 ns, 5 ns);
  component vbuf
    generic (tpd_a_y : VitalDelayType01);
    port (a : in std_logic; y : out std_logic);
  end component;
  signal a : std_logic := '0';
  signal y : std_logic_vector(0 to 1);
begin
  g : for i in 0 to 1 generate
    u : vbuf generic map (tpd_a_y => (r(i), f(i))) port map (a, y(i));
  end generate;

  stim : process
  begin
    wait for 10 ns;
    a <= '1';
    wait for 20 ns;
    a <= '0';
    wait;
  end process;

  m : for i in 0 to 1 generate
    mon : process (y(i))
    begin
      if y(i)'event then
        report "TRACE y" & integer'image(i) & "=" & std_logic'image(y(i))
          & " @" & time'image(now);
      end if;
    end process;
  end generate;
end architecture;
