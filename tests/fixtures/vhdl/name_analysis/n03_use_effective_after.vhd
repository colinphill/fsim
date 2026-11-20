-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.4: a use clause has effect only after it in the declarative part.
-- Expected: FSIM-VHDL-NAME-001 at line 10 (KQ used before the use clause).
package pq is
  constant KQ : integer := 3;
end package;
entity n03 is
end entity;
architecture a of n03 is
  constant before : integer := KQ;
  use work.pq.all;
  constant late : integer := KQ;
begin
end architecture;
