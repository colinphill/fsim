-- SPDX-License-Identifier: Apache-2.0
-- Foreign models registered by a tabular registry (IEEE 1076-2019 20.2.2):
-- foreign_vhpi_registry.reg names their elaboration and execution
-- functions.
entity foreign_inv is
  port (a : in bit; y : out bit);
end entity;

architecture model of foreign_inv is
  attribute foreign of model : architecture is
    "VHPI fsim_foreign_vhpi_registry_plugin inv";
begin
end architecture;

entity top is
end entity;

architecture t of top is
  function answer(n : integer) return integer is
  begin
    return 0;
  end function;
  attribute foreign of answer : function is
    "VHPI fsim_foreign_vhpi_registry_plugin answer";
  function seven(n : integer) return integer is
  begin
    return 0;
  end function;
  attribute foreign of seven : function is
    "VHPI fsim_foreign_vhpi_registry_plugin seven";
  signal a, y : bit;
  signal r : integer := 0;
begin
  u1 : entity work.foreign_inv port map (a => a, y => y);
  main : process
  begin
    wait for 1 ns;
    r <= answer(1) + seven(2);
    wait;
  end process;
end architecture;
