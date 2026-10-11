-- fsim-expect: FSIM-ELAB-VHRANGE-005 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 5.2.1: a range bound is an expression, not a type mark.
entity type_mark_bound is
end entity;

architecture a of type_mark_bound is
  type vector is array (positive to 7) of integer;
begin
end architecture;
