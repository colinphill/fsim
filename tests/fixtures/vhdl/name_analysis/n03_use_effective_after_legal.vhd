-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.4: control for n03: the name after the use clause resolves.
package pq is
  constant KQ : integer := 3;
end package;
entity n03l is
end entity;
architecture a of n03l is
  use work.pq.all;
  constant late : integer := KQ;
begin
end architecture;
