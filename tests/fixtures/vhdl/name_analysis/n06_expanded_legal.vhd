-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 8.3: expanded names with a process-label prefix, entity prefix and package prefix. Expected: success.
package p6 is
  constant PC : integer;
end package;
package body p6 is
  constant PC : integer := 4;
  constant PD : integer := p6.PC;
end package body;
entity n06b is
  generic (G : integer := 2);
  port (pin : in integer := 0);
end entity;
architecture a of n06b is
  constant e1 : integer := n06b.G;
begin
  proc : process
    variable v : integer := 0;
  begin
    proc.v := 1;
    v := pin;
    wait;
  end process;
end architecture;
