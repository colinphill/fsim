-- SPDX-License-Identifier: Apache-2.0
--
-- The delay element p1 with device_30.sdf: DEVICE z (6 ns) sets tdevice_p1_z
-- (IEEE 1076.4-2000 5.2.7.1.6). a is 0 at 0 ns, 1 at 10 ns and 0 at 30 ns, so
-- z is 0 at 6 ns, 1 at 16 ns and 0 at 36 ns.
library ieee;
use ieee.std_logic_1164.all;

entity tb_device is end entity;

architecture t of tb_device is
  component vdev port (a : in std_logic; z : out std_logic); end component;
  signal a : std_logic;
  signal z : std_logic;
begin
  p1 : vdev port map (a, z);

  stim : process
  begin
    a <= '0';
    wait for 10 ns;
    a <= '1';
    wait for 20 ns;
    a <= '0';
    wait;
  end process;

  mon : process (z)
  begin
    if z'event then
      report "TRACE z=" & std_logic'image(z) & " @" & time'image(now);
    end if;
  end process;
end architecture;
