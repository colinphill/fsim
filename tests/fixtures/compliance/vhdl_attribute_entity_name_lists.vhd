-- fsim: top=vhdl_attribute_entity_name_lists(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 7.2: `others` and `all` are reserved words that stand for
-- the whole entity name list, in any letter case; a named entity takes its
-- own specification, the others take the `others` one; an extended
-- identifier spelled \others\ is an ordinary entity designator.
entity vhdl_attribute_entity_name_lists is
end entity;

architecture sim of vhdl_attribute_entity_name_lists is
  attribute weight : integer;
  attribute tag : integer;
  signal a : bit;
  signal b : bit;
  signal \others\ : bit;
  constant c : integer := 3;
  attribute weight of a : signal is 1;
  attribute weight of \others\ : signal is 7;
  attribute weight of OTHERS : signal is 2;
  attribute tag of ALL : constant is 9;
begin
  process
  begin
    if a'weight /= 1 then
      report "FAIL named entity" severity error;
    elsif b'weight /= 2 then
      report "FAIL others entity" severity error;
    elsif \others\'weight /= 7 then
      report "FAIL extended identifier designator" severity error;
    elsif c'tag /= 9 then
      report "FAIL all entity" severity error;
    else
      report "PASS";
    end if;
    wait;
  end process;
end architecture;
