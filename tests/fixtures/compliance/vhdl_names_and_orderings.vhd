-- fsim: top=names_and_orderings(sim) std=1993
-- IEEE 1076-1993 1.1.2: constants in an entity declarative part; 2.1.1.2:
-- signal parameters of functions; 7.2.2: ordering of discrete arrays,
-- lexicographic from the left element; 3.2.1: index ranges and integer
-- type ranges given by range attributes; 6.4: indexed names of selected
-- record elements; 6.5: slices of slices; 14.1: 'ACTIVE of a signal
-- element.
entity names_and_orderings is
  constant base : integer := 10;
end entity;

architecture sim of names_and_orderings is
  type int_arr is array (1 to 3) of integer;
  type flags is array (1 to 3) of boolean;
  type rec is record
    re1 : flags;
    n : integer;
  end record;
  subtype word is bit_vector(0 to 7);
  type by_word is array (integer range word'range) of bit;
  type small is range word'range;
  signal s : bit_vector(0 to 3) := "0000";

  function doubled(signal v : bit_vector) return natural is
    variable count : natural := 0;
  begin
    for i in v'range loop
      if v(i) = '1' then
        count := count + 2;
      end if;
    end loop;
    return count;
  end function;
begin
  process
    variable a : int_arr := (1, -2, 3);
    variable b : int_arr := (1, 2, 0);
    variable r : rec := ((true, false, true), 5);
    variable bits : bit_vector(1 to 10) := "0011001100";
    variable w : by_word;
    variable k : small := 3;
    variable failures : natural := 0;
  begin
    if base /= 10 or not (a < b) or a >= b or not (a <= a) then
      failures := failures + 1;
    end if;
    if w'left /= 0 or w'right /= 7 or k /= 3 then
      failures := failures + 1;
    end if;
    if not r.re1(1) or r.re1(2) or bits(1 to 8)(3 to 6)(4 to 5) /= "10" then
      failures := failures + 1;
    end if;
    s <= "0110";
    wait for 0 ns;
    if not s(0)'active or doubled(s) /= 4 then
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
