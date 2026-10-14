-- fsim: top=vhdl_block_boolean_array_static_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Static Boolean-array values preserve nominal type, range direction, and null bounds.
entity vhdl_block_boolean_array_static_batch58 is
end entity;

architecture sim of vhdl_block_boolean_array_static_batch58 is
  subtype up_index is integer range 0 to 31;
  subtype down_index is integer range 31 downto 0;
  type up_boolean_v is array (up_index range <>) of boolean;
  type down_boolean_v is array (down_index range <>) of boolean;

  subtype up_four is up_boolean_v(4 downto 1);
  subtype up_eight is up_boolean_v(8 to 15);
  subtype down_four is down_boolean_v(7 downto 4);

  constant up_left : up_four := (true, false, true, false);
  constant up_right : up_boolean_v(12 to 15) :=
    (false, false, true, true);
  constant down_left : down_boolean_v(3 to 6) :=
    (true, false, true, false);
  constant down_right : down_boolean_v(20 downto 17) :=
    (false, true, false, true);
  constant up_single_desc : up_boolean_v(0 downto 0) := (others => true);
  constant up_single_asc : up_boolean_v(1 to 1) := (others => false);
  constant down_single_asc : down_boolean_v(31 to 31) := (others => true);
  constant down_single_desc : down_boolean_v(30 downto 30) := (others => false);
  constant null_up_asc : up_boolean_v(3 to 2) := (others => false);
  constant null_up_desc : up_boolean_v(6 downto 7) := (others => true);
  constant up_positional : up_boolean_v := (true, false, true);

  function return_down return down_four is
    constant value : down_four := (true, false, true, false);
  begin
    return value;
  end function;
begin
  ascending_base_range : block
    generic (info : up_boolean_v);
    generic map (up_left & up_right);
  begin
    assert info'left = 0 and info'right = 7 and info'ascending
      report "ascending concatenation did not use the base index subtype"
      severity failure;
    assert info = (true, false, true, false, false, false, true, true)
      report "ascending concatenation changed element order"
      severity failure;
  end block;

  constrained_reindex : block
    generic (info : up_eight);
    generic map (up_left & up_right);
  begin
    assert info'left = 8 and info'right = 15 and info'ascending
      report "constrained target did not slide the array positionally"
      severity failure;
    assert info = (true, false, true, false, false, false, true, true)
      report "constrained target changed Boolean elements"
      severity failure;
    assert info(8) and not info(9) and info(10) and not info(11)
      and not info(12) and not info(13) and info(14) and info(15)
      report "constrained generic array element reads changed values"
      severity failure;
  end block;

  descending_base_range : block
    generic (info : down_boolean_v);
    generic map (return_down & down_right);
  begin
    assert info'left = 31 and info'right = 24 and not info'ascending
      report "descending concatenation did not use the base index subtype"
      severity failure;
    assert info = (true, false, true, false, false, true, false, true)
      report "function-result concatenation changed element order"
      severity failure;
  end block;

  single_direction_concat : block
    generic (info : up_boolean_v);
    generic map (up_single_desc & up_single_asc);
  begin
    assert info'left = 0 and info'right = 1 and info'ascending
      report "single-element operand direction overrode the base subtype"
      severity failure;
    assert info(0) and not info(1)
      report "single-element concatenation changed element order"
      severity failure;
  end block;

  one_element_ascending : block
    generic (info : up_boolean_v);
    generic map (up_single_asc);
  begin
    assert info'left = 1 and info'right = 1 and info'ascending
      report "ascending one-element range lost its direction"
      severity failure;
    assert not info(1)
      report "ascending one-element value changed"
      severity failure;
  end block;

  one_element_descending : block
    generic (info : up_boolean_v);
    generic map (up_single_desc);
  begin
    assert info'left = 0 and info'right = 0 and not info'ascending
      report "descending one-element range lost its direction"
      severity failure;
    assert info(0)
      report "descending one-element value changed"
      severity failure;
  end block;

  descending_one_element_concat : block
    generic (info : down_boolean_v);
    generic map (down_single_asc & down_single_desc);
  begin
    assert info'left = 31 and info'right = 30 and not info'ascending
      report "descending base range was not retained for one-element operands"
      severity failure;
    assert info(31) and not info(30)
      report "descending one-element concatenation changed element order"
      severity failure;
  end block;

  unconstrained_positional_constant : block
    generic (info : up_boolean_v);
    generic map (up_positional);
  begin
    assert info'left = 0 and info'right = 2 and info'ascending
      report "unconstrained positional aggregate used incorrect bounds"
      severity failure;
    assert info = (true, false, true)
      report "unconstrained positional aggregate changed elements"
      severity failure;
  end block;

  null_left_operand : block
    generic (info : up_boolean_v);
    generic map (null_up_asc & up_right);
  begin
    assert info'left = 0 and info'right = 3 and info'ascending
      and info'length = 4
      report "left-null concatenation produced incorrect bounds"
      severity failure;
    assert info = (false, false, true, true)
      report "left-null concatenation changed the right operand"
      severity failure;
  end block;

  null_right_operand : block
    generic (info : up_boolean_v);
    generic map (up_left & null_up_asc);
  begin
    assert info'left = 0 and info'right = 3 and info'ascending
      and info'length = 4
      report "right-null concatenation produced incorrect bounds"
      severity failure;
    assert info = (true, false, true, false)
      report "right-null concatenation changed the left operand"
      severity failure;
  end block;

  both_null_operands : block
    generic (info : up_boolean_v);
    generic map (null_up_asc & null_up_desc);
  begin
    assert info'left = 6 and info'right = 7 and not info'ascending
      and info'length = 0
      report "both-null concatenation did not return the right operand"
      severity failure;
  end block;

  done : process
  begin
    wait for 1 ns;
    report "PASS" severity note;
    wait;
  end process;
end architecture;
