-- SPDX-License-Identifier: Apache-2.0
-- Disconnection specifications are objects of class disconnectSpec (IEEE
-- 1076-2019 7.4, 19.9, Figures 32 and 33): vhpiSpecs of a region lists them,
-- vhpiType is the type the specification names, vhpiTimeExpr its delay,
-- vhpiSignals the signals it names and vhpiAll / vhpiOthers the `all` or
-- `others` it is written with.
entity top is
end entity;

architecture t of top is
  signal b1, b2 : bit bus;
  signal n : integer bus;
  disconnect b1, b2 : bit after 5 ns;
  disconnect others : integer after 7 ns;
begin
end architecture;
