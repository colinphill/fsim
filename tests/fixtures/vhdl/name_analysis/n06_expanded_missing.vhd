-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 8.3: the selected name work.p1.missing has no such declaration in p1.
-- Expected: FSIM-VHDL-NAME-003 at line 10.
package p1 is
  constant K : integer := 1;
end package;
entity n06a is
end entity;
architecture a of n06a is
  constant c : integer := work.p1.missing;
begin
end architecture;
