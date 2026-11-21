-- fsim: top=d8_generic_default_composite(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.5.6.2, 14.4.2.1: a generic left unassociated takes its
-- default; a record generic with a default is a constant object that
-- an external name (8.7) reads.
package d8_gdc_pkg is
  type rec is record
    a : integer;
    b : bit_vector(1 downto 0);
  end record;
  constant rec_zero : rec := (a => 7, b => "10");
end package;

use work.d8_gdc_pkg.all;
entity d8_gdc_unit is
  generic (
    param_rec : rec := rec_zero);
end entity;
architecture a of d8_gdc_unit is
begin
end architecture;

use work.d8_gdc_pkg.all;
entity d8_generic_default_composite is
end entity;
architecture test of d8_generic_default_composite is
begin
  u : entity work.d8_gdc_unit;
  process
    alias g is <<constant .d8_generic_default_composite.u.param_rec : rec>>;
  begin
    wait for 1 ns;
    assert g.a = 7 and g.b = "10" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
