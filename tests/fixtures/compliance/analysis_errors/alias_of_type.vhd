-- fsim-expect: FSIM-ELAB-VHALIAS-004 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 4.3.3: only an object alias has a subtype indication.
entity alias_of_type is
end entity;

architecture a of alias_of_type is
  type x is range 1 to 10;
  alias y : x is x;
begin
end architecture;
