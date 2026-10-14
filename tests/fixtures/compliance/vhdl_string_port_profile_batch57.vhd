-- fsim: top=vhdl_string_port_profile_batch57(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0

package vhdl_string_port_profile_batch57_types is
  type character_array is array (natural range <>) of character;
end package;

use work.vhdl_string_port_profile_batch57_types.all;
entity vhdl_string_port_profile_batch57 is
  port (
    builtin_text : in string(1 to 3) := "abc";
    user_text : in character_array(0 to 2) := "abc"
  );
end entity;

architecture sim of vhdl_string_port_profile_batch57 is
  function select_text(value : string) return integer;
  function select_text(value : character_array) return integer;
  function make_text return string;
  function make_characters return character_array;

  function select_text(value : string) return integer is
  begin
    return 1;
  end function;

  function select_text(value : character_array) return integer is
  begin
    return 0;
  end function;

  function make_text return string is
  begin
    return "abc";
  end function;

  function make_characters return character_array is
  begin
    return "abc";
  end function;

  procedure check_text(value : in string; result : out boolean) is
  begin
    result := value = "abc";
  end procedure;
begin
  check_profiles : process
    variable result : boolean;
  begin
    assert select_text(builtin_text) = 1
      report "FAIL STRING port selected the character_array overload"
      severity failure;
    assert select_text(user_text) = 0
      report "FAIL character_array port selected the STRING overload"
      severity failure;
    assert select_text(make_characters) = 0
      report "FAIL character_array result selected the STRING overload"
      severity failure;
    assert select_text(make_text) = 1
      report "FAIL STRING function result selected an incompatible profile"
      severity failure;
    assert select_text("a" & make_text) = 1
      report "FAIL STRING concatenation selected an incompatible profile"
      severity failure;
    check_text(builtin_text, result);
    assert result
      report "FAIL STRING port actual was not accepted by the procedure"
      severity failure;
    check_text("" & make_text, result);
    assert result
      report "FAIL STRING concatenation was not accepted by the procedure"
      severity failure;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;
