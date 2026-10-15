-- fsim: top=vhdl_string_function_result_bounds_batch59(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Function results preserve explicit bounds through aliases and call chains.
entity vhdl_string_function_result_bounds_batch59 is
end entity;

architecture sim of vhdl_string_function_result_bounds_batch59 is
  type marked_text is array (positive range <>) of character;
  constant other_text : marked_text(1 to 15) := "hello, world! x";
  signal current_character : character := 'x';

  function change_bounds(value : string; left_bound, right_bound : positive)
      return string is
    alias rebased : string(left_bound to right_bound) is value;
  begin
    return rebased;
  end function;

  function change_to_null(value : string) return string is
    alias empty : string(101 to 100) is value;
  begin
    return empty;
  end function;

  impure function make_text(value : character) return string is
    variable text : string(1 to 15);
  begin
    text := "hello, world! " & value;
    return change_bounds(text, 101, 115);
  end function;

  function make_empty return string is
    variable empty : string(1 to 0);
  begin
    return change_to_null(empty);
  end function;

  function get_left(value : string) return integer is
  begin
    return value'left;
  end function;

  function get_right(value : string) return integer is
  begin
    return value'right;
  end function;

  function get_ascending(value : string) return boolean is
  begin
    return value'ascending;
  end function;

  function get_length(value : string) return integer is
  begin
    return value'length;
  end function;

  function family_of(value : string) return integer is
  begin
    return 1;
  end function;

  function family_of(value : marked_text) return integer is
  begin
    return 2;
  end function;
begin
  check : process
    variable text : string(1 to 15);
    variable empty : string(1 to 0);
  begin
    text := make_text(current_character);
    assert text = "hello, world! x"
        and get_left(make_text(current_character)) = 101
        and get_right(make_text(current_character)) = 115
        and get_ascending(make_text(current_character))
        and get_length(make_text(current_character)) = 15
      report "STRING function result lost explicit alias bounds"
      severity failure;

    empty := make_empty;
    assert empty'length = 0
        and get_left(make_empty) = 101
        and get_right(make_empty) = 100
        and get_ascending(make_empty)
        and get_length(make_empty) = 0
      report "null STRING function result lost its explicit bounds"
      severity failure;

    assert family_of(other_text) = 2
      report "a distinct CHARACTER-array type selected the STRING overload"
      severity failure;

    report "PASS" severity note;
    wait;
  end process;
end architecture;
