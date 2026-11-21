-- fsim: top=d8_formal_conversion_generic_subtype(a) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 4.3.2.2: a formal designator may be a type conversion of
-- the formal port. The subtype of the signal that stands for the converted
-- port is that of the port, whose bounds may use a generic of the
-- component (4.5): the generic is visible there and not in the
-- architecture that holds the association.
entity d8_fc_leaf is
  generic (width : natural := 4);
  port (q : out bit_vector(0 to width - 1));
end entity;

architecture a of d8_fc_leaf is
begin
  q <= (others => '1');
end architecture;

entity d8_formal_conversion_generic_subtype is
end entity;

architecture a of d8_formal_conversion_generic_subtype is
  type word is array (natural range <>) of bit;
  component d8_fc_leaf is
    generic (width : natural := 4);
    port (q : out bit_vector(0 to width - 1));
  end component;
  signal s : word(0 to 3);
begin
  u : d8_fc_leaf generic map (width => 4) port map (word(q) => s);

  process
  begin
    wait for 1 ns;
    assert s = "1111" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
