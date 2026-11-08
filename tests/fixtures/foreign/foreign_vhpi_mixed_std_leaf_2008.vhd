-- SPDX-License-Identifier: Apache-2.0
-- Compiled as VHDL-2008 and instantiated by a VHDL-2019 or VHDL-2008 top.
-- A design with a VHDL-2019 unit has one 64-bit INTEGER for every unit
-- (IEEE 1076-2019 5.2.3.1; IEEE 1076-2008 5.2.3.1 sets only the minimum
-- -(2**31-1) to 2**31-1, so 64 bits is legal for a 2008 unit): hi holds
-- integer'high, 2**63-1 = 9223372036854775807 there and 2**31-1 =
-- 2147483647 otherwise. q is p + 1.
entity leaf is
  port (p : in integer; q : out integer);
end entity;

architecture a of leaf is
  signal hi : integer := integer'high;
begin
  q <= p + 1;
end architecture;
