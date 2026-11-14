-- SPDX-License-Identifier: Apache-2.0
-- A call of a package procedure whose body is declared apart from its
-- specification (IEEE 1076-2019 4.8, 14.6 b): the frame of the call has the
-- formal and the variables the body declares.
package pk is
  procedure down(n : integer);
end package;
package body pk is
  procedure down(n : integer) is
    variable depth : integer := n * 10;
    variable x : integer;
  begin
    depth := depth + 1;
    x := depth;
    if n > 1 then
      down(n - 1);
    end if;
  end procedure;
end package body;
use work.pk.all;
entity top is end entity;
architecture rtl of top is
  signal o : integer := 0;
begin
  main : process is
  begin
    down(2);
    wait for 1 ns;
    wait;
  end process;
end architecture;
