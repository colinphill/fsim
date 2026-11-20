-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.4: two use-visible homographs that are not overloadable hide each other.
-- Expected: FSIM-VHDL-NAME-002 at line 15 (K is potentially visible from p1 and p2).
package p1 is
  constant K : integer := 1;
end package;
package p2 is
  constant K : integer := 2;
end package;
use work.p1.all;
use work.p2.all;
entity n04a is
end entity;
architecture a of n04a is
  constant c : integer := K;
begin
end architecture;
