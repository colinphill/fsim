-- fsim-expect: FSIM-VHDL-SEM-128 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 9.1: a block guard expression is BOOLEAN.
entity integer_guard is
end entity;

architecture a of integer_guard is
begin
  b : block (1 + 2)
  begin
  end block;
end architecture;
