-- fsim: top=vhdl_user_attribute_subtype_context(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 7.2: user-defined attribute values retain their declared
-- subtype for package, entity, function, array-index, and aggregate contexts.
package vhdl_user_attribute_subtype_context_types is
  type character_pair is array (1 to 2) of character;
  attribute scalar_bit : bit;
  attribute locations : character_pair;
  attribute scalar_bit of vhdl_user_attribute_subtype_context_types : package is '0';
end package;

use work.vhdl_user_attribute_subtype_context_types.all;
entity vhdl_user_attribute_subtype_context is
  attribute scalar_bit of vhdl_user_attribute_subtype_context : entity is '1';
end entity;

use work.vhdl_user_attribute_subtype_context_types.all;
architecture sim of vhdl_user_attribute_subtype_context is
  signal payload : integer;

  function function_value return bit is
  begin
    return '0';
  end function;

  attribute scalar_bit of function_value : function is '0';
  attribute locations of payload : signal is ('A', 'B');
begin
  process
  begin
    if vhdl_user_attribute_subtype_context_types'scalar_bit /= '0' then
      report "FAIL package attribute subtype" severity error;
    elsif vhdl_user_attribute_subtype_context'scalar_bit /= '1' then
      report "FAIL entity attribute subtype" severity error;
    elsif function_value'scalar_bit /= '0' then
      report "FAIL function attribute subtype" severity error;
    elsif payload'locations(1) /= 'A'
      or payload'locations /= ('A', 'B') then
      report "FAIL composite attribute subtype" severity error;
    else
      report "PASS";
    end if;
    wait;
  end process;
end architecture;
