-- SPDX-License-Identifier: Apache-2.0
-- The port map of a binding indication associates parts of the entity's
-- ports with parts of the component's ports (IEEE 1076-2019 7.3.2.3,
-- 6.5.7.1); each association element has the part of the entity port as
-- vhpiFormal and the part of the component port as vhpiActual (19.5, 19.12).
entity ent is
  port (p : in bit_vector(3 downto 0); q : out bit);
end entity;

architecture e of ent is
begin
  q <= p(0);
end architecture;

entity top is
end entity;

architecture t of top is
  component cmp is
    port (a : in bit_vector(3 downto 0); b : out bit);
  end component;
  for u : cmp use entity work.ent
    port map (p(3 downto 2) => a(3 downto 2), p(1 downto 0) => a(1 downto 0),
              q => b);
  signal sa : bit_vector(3 downto 0);
  signal sb : bit;
begin
  u : cmp port map (a => sa, b => sb);
end architecture;
