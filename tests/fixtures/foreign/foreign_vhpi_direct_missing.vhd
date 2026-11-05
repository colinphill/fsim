-- SPDX-License-Identifier: Apache-2.0
-- A standard direct binding whose execution function the object library
-- does not provide (IEEE 1076-2019 20.2.4.3): the run fails at load.
entity foreign_inv is
  port (a : in bit; y : out bit);
end entity;

architecture model of foreign_inv is
  attribute foreign of model : architecture is
    "VHPIDIRECT null direct_elab no_such_function";
begin
end architecture;

entity top is
end entity;

architecture t of top is
  signal a, y : bit;
begin
  u1 : entity work.foreign_inv port map (a => a, y => y);
end architecture;
