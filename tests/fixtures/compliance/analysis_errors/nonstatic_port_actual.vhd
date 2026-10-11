-- fsim-expect: FSIM-ELAB-VHPORT-005 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 1.1.1.2: a port actual that is a name is a static name.
entity nonstatic_port_leaf is
  port (f : in bit);
end entity;

architecture a of nonstatic_port_leaf is
begin
end architecture;

entity nonstatic_port_actual is
  port (bits : in bit_vector(1 to 8); index : in integer range 1 to 8);
end entity;

architecture a of nonstatic_port_actual is
begin
  leaf : entity work.nonstatic_port_leaf port map (bits(index));
end architecture;
