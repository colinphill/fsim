-- fsim: top=vhdl_aggregate_bounded_element_layout(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- A positional array aggregate counts each bounded array or record element
-- once, even when the element value itself is written as an aggregate.
entity vhdl_aggregate_bounded_element_layout is
end entity;

architecture sim of vhdl_aggregate_bounded_element_layout is
  subtype str16 is string(1 to 16);
  type string_table is array (1 to 5) of str16;
  subtype byte is bit_vector(7 downto 0);
  type byte_table is array (1 to 3) of byte;
  type byte_vector is array (natural range <>) of byte;
  type pair is record
    first, second : integer;
  end record;
  type pair_table is array (1 to 2) of pair;
  type pair_vector is array (natural range <>) of pair;
  type int_matrix is array (natural range <>, natural range <>) of integer;

  function matrix_shape(value : int_matrix) return boolean is
  begin
    return value'left(1) = 0 and value'right(1) = 1
      and value'left(2) = 0 and value'right(2) = 1
      and value(0, 0) = 1 and value(1, 1) = 4;
  end function;

  constant strings : string_table := (
    "This is string 1",
    "__Hello  World__",
    "This is string " & "3",
    "_Bird is a word_",
    "_Goodbye (ciao)_");
  constant bytes : byte_table := (X"01", X"02", X"03");
  constant pairs : pair_table := ((1, 2), (3, 4));
  constant byte_values : byte_vector(1 to 3)
    := (X"01", X"02", X"03");
  constant text_value : string(1 to 3) := "abc";
  constant pair_values : pair_vector(1 to 3) := ((1, 2), (3, 4), (5, 6));
begin
  process
  begin
    assert strings = (
      "This is string 1",
      "__Hello  World__",
      "This is string " & "3",
      "_Bird is a word_",
      "_Goodbye (ciao)_");
    assert bytes = (X"01", X"02", X"03");
    assert pairs = ((1, 2), (3, 4));
    assert byte_values /= (X"01", X"02");
    assert text_value /= ('a', 'b');
    assert pair_values /= ((1, 2), (3, 4));
    assert matrix_shape(((1, 2), (3, 4)));
    report "PASS";
    wait;
  end process;
end architecture;
