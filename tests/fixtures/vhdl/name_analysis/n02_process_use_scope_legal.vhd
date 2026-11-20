-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.4: control for n02: the use clause serves the process that contains it.
package pk is
  constant KP : integer := 3;
end package;
entity n02l is
end entity;
architecture a of n02l is
begin
  p1 : process
    use work.pk.all;
    variable v : integer := KP;
  begin
    wait;
  end process;
end architecture;
