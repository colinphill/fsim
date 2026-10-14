-- fsim: top=vhdl_character_array_context_batch57(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0

package vhdl_character_array_context_batch57_types is
  type character_array is array (natural range <>) of character;
end package;

use work.vhdl_character_array_context_batch57_types.all;
entity vhdl_character_array_context_batch57 is
end entity;

architecture sim of vhdl_character_array_context_batch57 is
  subtype character_triple is character_array(0 to 2);
  function classify(value : character_array) return integer;
  function make_characters return character_triple;

  function classify(value : character_array) return integer is
  begin
    return value'length;
  end function;

  function make_characters return character_triple is
  begin
    return "abc";
  end function;
begin
  check_context : process
  begin
    assert classify("abc") = 3
      report "FAIL quoted literal did not resolve as a CHARACTER array"
      severity failure;
    assert classify("010") = 3
      report "FAIL BIT-like text was sized as a packed BIT string"
      severity failure;
    assert classify(make_characters) = 3
      report "FAIL parameterless function return lost its array profile"
      severity failure;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;
