-- fsim: top=vhdl_package_bitvector_scope(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- A package subtype retains the standard BIT_VECTOR visible where the
-- subtype is declared, even when its caller declares a local BIT_VECTOR.
package b57_bitvector_types is
  subtype word_t is bit_vector(3 downto 0);
  type word_array_t is array (natural range <>) of word_t;
end package;

use work.b57_bitvector_types.all;
entity vhdl_package_bitvector_scope is
end entity;

architecture sim of vhdl_package_bitvector_scope is
  type bit_vector is array (natural range <>) of bit;
  constant contents : word_array_t(0 to 1) :=
    (0 => X"A", 1 => X"5");
begin
  process
  begin
    assert contents(0) = X"A" and contents(1) = X"5"
      report "FAIL package BIT_VECTOR subtype uses caller shadow"
      severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
