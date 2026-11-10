-- SPDX-License-Identifier: Apache-2.0
--
-- A loop parameter is a constant of the base type of its discrete range
-- (IEEE 1076-2019 10.10). T'LOW, T'HIGH, T'LEFT and T'RIGHT of BOOLEAN or of
-- a subtype of it are values of BOOLEAN (16.2.2), T'RANGE and
-- T'REVERSE_RANGE are ranges of it, and so is the range of a subtype of
-- BOOLEAN given by its type mark (5.2.1, 5.2.2.1): in every one of these
-- loops the parameter is a BOOLEAN wherever its name is used, as in a loop
-- over the type mark BOOLEAN or the literals FALSE to TRUE. It is the source
-- of an assignment to a BOOLEAN variable (10.6), an index of an array indexed
-- by BOOLEAN (8.4), a case selector whose choices are BOOLEAN literals
-- (10.9), an operand of NOT, AND and "=" (9.2), an actual of a BOOLEAN
-- formal (4.2.2.2) and the argument of 'IMAGE and 'POS (16.2.2).

entity top is
end entity;

architecture test of top is
  subtype up is boolean range false to true;
  subtype down is boolean range true downto false;
  subtype only_true is boolean range true to true;

  function same_boolean (value : boolean) return boolean is
  begin
    return value;
  end function;
begin
  process
    type by_boolean is array (boolean) of integer;
    variable hits : by_boolean := (others => 0);
    variable flag : boolean;
    variable selected : character;
  begin
    for b in boolean'low to boolean'high loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop low_high " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in boolean'left to boolean'right loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop left_right " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in boolean'high downto boolean'low loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop high_downto_low " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in boolean'range loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop range " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in boolean'reverse_range loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop reverse_range " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in up loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop subtype " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in up'range loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop subtype_range " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in up'reverse_range loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop subtype_reverse_range " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in up'low to up'high loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop subtype_low_high " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in down loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop descending_subtype " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in down'left downto down'right loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop descending_left_right " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in only_true'low to only_true'high loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop single " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in boolean loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop type_mark " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    for b in false to true loop
      flag := b;
      hits(b) := hits(b) + 1;
      case b is
        when true => selected := 't';
        when false => selected := 'f';
      end case;
      report "boolloop literals " & boolean'image(b) & " "
        & boolean'image(not b) & " " & boolean'image(b = true) & " "
        & boolean'image(b and true) & " " & boolean'image(same_boolean(b))
        & " " & boolean'image(flag) & " " & integer'image(boolean'pos(b))
        & " " & selected;
    end loop;
    report "boolloop hits " & integer'image(hits(false)) & " "
      & integer'image(hits(true));
    wait;
  end process;
end architecture;
