-- SPDX-License-Identifier: Apache-2.0
-- Standard direct bindings (IEEE 1076-2019 20.2.4.3): the architecture and
-- the function Answer name no registered model but entry points of the
-- VHPI program's object library. A null object library specifier lets the
-- tool find the library, and a null execution specifier names the function
-- after the subprogram designator, letter case included; seven names the
-- object library and the function.
entity foreign_inv is
  port (a : in bit; y : out bit);
end entity;

architecture model of foreign_inv is
  attribute foreign of model : architecture is
    "VHPIDIRECT null direct_elab direct_exec";
begin
end architecture;

entity top is
end entity;

architecture t of top is
  function Answer(n : integer) return integer is
  begin
    return 0;
  end function;
  attribute foreign of Answer : function is "VHPIDIRECT null null";
  function seven(n : integer) return integer is
  begin
    return 0;
  end function;
  attribute foreign of seven : function is
    "VHPIDIRECT fsim_foreign_vhpi_direct_plugin direct_seven";
  signal a, y : bit;
  signal r : integer := 0;
begin
  u1 : entity work.foreign_inv port map (a => a, y => y);
  main : process
  begin
    wait for 1 ns;
    r <= Answer(1) + seven(2);
    wait;
  end process;
end architecture;
