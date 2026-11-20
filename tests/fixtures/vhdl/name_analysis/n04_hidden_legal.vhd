-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.3: a local declaration of K hides both use-visible Ks. Expected: success.
package p1 is
  constant K : integer := 1;
end package;
package p2 is
  constant K : integer := 2;
end package;
use work.p1.all;
use work.p2.all;
entity n04b is
end entity;
architecture a of n04b is
  constant K : integer := 5;
  constant c : integer := K;
begin
end architecture;
