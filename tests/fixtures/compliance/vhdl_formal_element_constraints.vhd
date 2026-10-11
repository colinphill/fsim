-- fsim: top=formal_element_constraints(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 4.2.2.2 and 5.3.2.2: an unbounded array formal takes its
-- index ranges and element constraints from its actual; 8.4: indexed names
-- of record elements of array elements; 5.3.2.1: a table indexed by
-- STD_ULOGIC with character literal indices.
library ieee;
use ieee.std_logic_1164.all;

entity formal_element_constraints is
end entity;

architecture sim of formal_element_constraints is
  type int_array is array (integer range <>) of integer_vector;
  type rec is record
    x : integer_vector;
  end record;
  type rec_vec is array (natural range <>) of rec;
  type u_table is array (std_ulogic) of integer;
  constant weights : u_table := ('U' => 9, '0' => 0, '1' => 1, others => 5);

  function total(x : int_array) return integer is
    variable t : integer := 0;
  begin
    for i in x'range loop
      for j in x(i)'range loop
        t := t + x(i)(j);
      end loop;
    end loop;
    return t;
  end function;

  signal s1 : int_array(1 to 3)(1 to 2) := ((1, 2), (3, 4), (5, 6));
  signal t : rec_vec(1 to 2)(x(1 to 3));
begin
  process
    variable failures : natural := 0;
  begin
    if total(s1) /= 21 or s1(2)'length /= 2 then
      failures := failures + 1;
    end if;
    t(1).x(2) <= 7;
    t(2).x <= (1, 2, 3);
    wait for 1 ns;
    if t(1).x(2) /= 7 or t(2).x(3) /= 3 then
      failures := failures + 1;
    end if;
    if weights('U') /= 9 or weights('1') /= 1 or weights('Z') /= 5 then
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
