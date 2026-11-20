-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 8.3: an expanded name selects one K though both are use-visible. Expected: success.
package p1 is
  constant K : integer := 1;
end package;
package p2 is
  constant K : integer := 2;
end package;
use work.p1.all;
use work.p2.all;
entity n04c is
end entity;
architecture a of n04c is
  constant c : integer := work.p1.K;
  constant d : integer := work.p2.K;
begin
end architecture;
