-- fsim: top=d8_operator_alias_library(a) std=1993 before=d8_operator_alias_library_pkg.vhd.in
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 6.6.3, 12.4: an operator alias with a signature, declared
-- in a package analyzed in an earlier compile, is visible through a use
-- clause and denotes its target function.
use work.d8_oal_utils.all;
entity d8_operator_alias_library is
end entity;
architecture a of d8_operator_alias_library is
begin
  process
    variable v : bit_vector(3 downto 0) := "0000";
  begin
    v := v + "0001";
    assert v = "0001" severity failure;
    v := v + v;
    assert v = "0000" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
