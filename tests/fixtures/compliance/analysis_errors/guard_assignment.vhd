-- fsim-expect: FSIM-VHDL-SEM-127 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 9.1: the implicit GUARD signal of a guarded block has no
-- sources.
entity guard_assignment is
end entity;

architecture a of guard_assignment is
  signal p : boolean := true;
begin
  b : block (p)
  begin
    guard <= p;
  end block;
end architecture;
