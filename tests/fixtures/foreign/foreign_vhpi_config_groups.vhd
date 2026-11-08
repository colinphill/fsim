-- SPDX-License-Identifier: Apache-2.0
-- Range bounds of enumeration, physical and floating point subtypes
-- (IEEE 1076-2019 19.4.3, Figure 7), groups and attribute specifications
-- (6.9, 7.2, Figure 12) and a configuration declaration (7.3.1, 19.5,
-- Figure 3).
entity leaf is
  port (i : in bit; o : out bit);
end entity;

architecture rtl of leaf is
begin
  o <= i;
end architecture;

entity top is
end entity;

architecture a of top is
  component leaf is
    port (i : in bit; o : out bit);
  end component;
  type color is (red, green, blue);
  subtype warm is color range red to green;
  type length is range 0 to 1000000
    units
      mm;
      cm = 10 mm;
    end units;
  subtype short is length range 5 mm to 20 mm;
  subtype frac is real range 0.0 to 1.0;
  signal x, y : bit;
  constant c : integer := 2;
  signal v : bit_vector(3 downto 0) := "1010";
  signal q : bit;
  signal a1, a2 : bit;
  group pairt is (signal, signal <>);
  group g1 : pairt (a1, a2);
  attribute tag : string;
  attribute tag of a1, a2 : signal is "x";
begin
  u1 : leaf port map (i => x, o => y);
  -- v(c + 1) is a locally static name (9.4.2): c is a constant of known
  -- value, so v(3) = '1' reads with a value (IEEE 1076-2019 22.3).
  p : process
  begin
    q <= v(c + 1);
    wait;
  end process;
end architecture;

configuration cfg of top is
  for a
    for u1 : leaf
      use entity work.leaf(rtl);
    end for;
  end for;
end configuration;
