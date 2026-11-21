-- fsim: top=d8_view_element_subtypes_tb(a) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 6.5.2: the element subtypes of a mode view are the ones
-- written in the view's record type declaration. They denote what is
-- visible there, not at the entity that uses the view, even when the
-- entity's use clause follows the package in the same file (12.4).
package d8_view_pack is
  subtype word is bit_vector(1 to 3);

  type pair_t is record
    a : word;
    b : natural;
  end record;

  view pair_in of pair_t is
    a : in;
    b : in;
  end view;
end package;

use work.d8_view_pack.all;

entity d8_view_sub is
  port (i : view pair_in; ok : out boolean);
end entity;

architecture a of d8_view_sub is
begin
  ok <= i.a = "101" and i.b = 7;
end architecture;

use work.d8_view_pack.all;

entity d8_view_element_subtypes_tb is
end entity;

architecture a of d8_view_element_subtypes_tb is
  signal s  : pair_t := (a => "101", b => 7);
  signal ok : boolean;
begin
  u : entity work.d8_view_sub port map (i => s, ok => ok);

  process
  begin
    wait for 1 ns;
    assert ok severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
