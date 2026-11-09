-- SPDX-License-Identifier: Apache-2.0
-- The design unit classes of the library information model (IEEE 1076-2019
-- 19.4.5, Figure 4): vhpiDecls of every design unit, vhpiGenericDecls,
-- vhpiPortDecls and vhpiStmts of an entity declaration, vhpiStmts of an
-- architecture body, and vhpiDepUnits, which includes STD.STANDARD because
-- every design unit starts with `library STD, WORK; use STD.STANDARD.all;`
-- (12.4). The library STD holds STANDARD.
package pk is
  constant c : integer := 3;
  type color is (red, green);
end package;

entity leaf is
  port (x : in bit);
end entity;

architecture l of leaf is
begin
end architecture;

entity top is
  generic (g : integer := 2);
  port (a : in bit; q : out bit);
  constant ec : integer := 5;
begin
  chk : assert true report "entity statement";
end entity;

-- No unit of the design uses pk: its declarations are read from its library
-- object when a program asks for them (19.4.5).
architecture t of top is
  signal s : bit;
  constant ac : integer := 7;
  component leaf
    port (x : in bit);
  end component;
begin
  p1 : process (a)
  begin
    s <= a;
  end process;
  q <= s;
  u1 : leaf port map (x => s);
  u3 : entity work.leaf port map (x => s);
  b1 : block
  begin
    u2 : leaf port map (x => s);
  end block;
end architecture;
