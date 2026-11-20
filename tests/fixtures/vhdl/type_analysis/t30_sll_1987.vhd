-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1987 7.2, 13.9: shift operators and the reserved word sll do not exist before
-- VHDL-1993; the front end rejects the operator for the selected edition.
-- Expected: FSIM-FE-VHSTD-003.
-- fsim-standard: 1987
entity t30 is
end;
architecture a of t30 is
begin
  process
    variable v : bit_vector(1 to 4);
  begin
    v := v sll 1;
    wait;
  end process;
end;
