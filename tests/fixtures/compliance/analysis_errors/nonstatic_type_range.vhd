-- fsim-expect: FSIM-ELAB-VHRANGE-004 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 3.1.2: an integer type definition has a locally static range.
entity nonstatic_type_range is
  generic (low : integer := 0);
end entity;

architecture a of nonstatic_type_range is
  type count is range low to 1000;
begin
end architecture;
