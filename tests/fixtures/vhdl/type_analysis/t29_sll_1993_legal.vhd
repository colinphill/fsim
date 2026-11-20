-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 7.2.3: sll is a predefined shift operator on BIT_VECTOR.
-- Expected: success.
-- fsim-standard: 1993
entity t29 is
end entity;
architecture a of t29 is
begin
  process
    variable v : bit_vector(1 to 4);
  begin
    v := v sll 1;
    wait;
  end process;
end architecture;
