-- SPDX-License-Identifier: Apache-2.0
-- The parameter of a `for` loop of a process (IEEE 1076-2019 10.10, 14.6 a):
-- it is a constant of the loop, declared when the loop is elaborated, and its
-- value is readable while the loop runs. It is a constant (class constDecl),
-- which 22.5.1 does not list among the objects whose value a VHPI program
-- may update, so a put to it is an error.
entity top is
end entity;

architecture rtl of top is
begin
  main : process is
    variable total : integer := 0;
  begin
    for i in 1 to 3 loop
      total := total + i * 10;
    end loop;
    for j in 7 downto 6 loop
      total := total + j;
    end loop;
    wait;
  end process;
end architecture;
