-- SPDX-License-Identifier: Apache-2.0
-- One component instantiated twice: u1 under the default binding (IEEE
-- 1076-2019 7.3.3) and u2 under a binding indication whose port map names
-- the local ports (7.3.2.1, 6.5.7.3). Both elaborate to the same block
-- hierarchy (14.5.4, 11.8.2), so the VHPI model of the two instances is the
-- same: the ports of the bound design entity with the actuals' values, and
-- the instance's port association elements.
entity cmp is
  port (r : in bit; s : out bit);
end entity;

architecture e of cmp is
begin
  s <= r;
end architecture;

entity top is
end entity;

architecture t of top is
  component cmp is
    port (r : in bit; s : out bit);
  end component;
  for u2 : cmp use entity work.cmp port map (r => r, s => s);
  signal sa1, sd1, sa2, sd2 : bit;
begin
  u1 : cmp port map (r => sa1, s => sd1);
  u2 : cmp port map (r => sa2, s => sd2);
  sa1 <= '1';
  sa2 <= '1';
end architecture;
