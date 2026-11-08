-- SPDX-License-Identifier: Apache-2.0
-- vhpiResolFunc (IEEE 1076-2019 19.4.3, Figure 14): the resolution function
-- named in a signal's subtype indication, and for STD_LOGIC the function
-- RESOLVED of IEEE.STD_LOGIC_1164 (16.7: function resolved (s :
-- std_ulogic_vector) return std_ulogic). A subtype declaration names its
-- function too (Figure 22). A signal of an unresolved type has none.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture t of top is
  type int_array is array (natural range <>) of integer;
  function sum_i (v : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in v'range loop
      total := total + v(i);
    end loop;
    return total;
  end function;
  subtype rs is sum_i integer;
  signal a : std_logic;
  signal b : sum_i integer;
  signal c : std_ulogic;
begin
end architecture;
