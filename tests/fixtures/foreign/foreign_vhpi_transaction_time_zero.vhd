-- SPDX-License-Identifier: Apache-2.0
-- Transactions of the first simulation cycle (IEEE 1076-2019 14.7.5.2,
-- 14.7.5.3, 21.3.2.5, 21.3.7.6): the initialization phase only schedules the
-- assignments, so callbacks registered at vhpiCbStartOfSimulation see their
-- transactions and value changes at time zero, in a VHDL-only design and in
-- one mixed with SystemVerilog.
entity top is
end entity;

architecture a of top is
  signal s : bit_vector(3 downto 0) := "0000";
  signal t : integer := 0;
begin
  s <= "1010";
  t <= 0;
end architecture;
