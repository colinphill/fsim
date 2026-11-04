-- SPDX-License-Identifier: Apache-2.0
-- The save/restart data functions outside a save or restart (IEEE 1076-2019
-- 20.7, 20.8, 23.12, 23.27).
entity top is
end entity;

architecture a of top is
  signal s : bit := '0';
begin
  s <= '1' after 1 ns;
end architecture;
