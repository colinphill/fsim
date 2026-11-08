-- SPDX-License-Identifier: Apache-2.0
-- vhpiGenIndex of a block configuration whose index is a static expression of
-- a constant (IEEE 1076-2019 3.4.2, 19.5, Figure 3): `k - 1` with k = 2 is 1.
-- The relation leads to an expression object whose value is the index.
package pk is
  constant k : integer := 2;
end package;

entity leaf is
  port (i : in bit; o : out bit);
end entity;

architecture rtl of leaf is
begin
  o <= i;
end architecture;

entity top is
end entity;

use work.pk.all;
architecture a of top is
  component leaf is
    port (i : in bit; o : out bit);
  end component;
  signal x : bit_vector(0 to 2);
  signal y : bit_vector(0 to 2);
begin
  g : for n in 0 to 2 generate
    u : leaf port map (i => x(n), o => y(n));
  end generate;
end architecture;

use work.pk.all;
configuration cfg of top is
  for a
    for g(k - 1)
      for u : leaf
        use entity work.leaf(rtl);
      end for;
    end for;
  end for;
end configuration;
