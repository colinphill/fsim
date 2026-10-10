-- fsim: top=aliases_and_array_formals(sim) std=1993
-- IEEE 1076-1993 4.3.3.1: object aliases of slices and elements, with the
-- alias subtype's own index range; 2.1.1.1 and 3.2.1.1: subprogram formals of
-- unconstrained array types (STRING, arrays of INTEGER) take the index range
-- of their actuals, and functions return such arrays; 13.4.1: integer
-- literals with an exponent.
entity aliases_and_array_formals is
end entity;

architecture sim of aliases_and_array_formals is
  type int_array is array (natural range <>) of integer;
  type distance is range 0 to 2E9
    units
      mil;
      inch = 1000 mil;
      ft = 12 inch;
    end units;

  function count_of(s : string; c : character) return natural is
    variable n : natural := 0;
  begin
    for i in s'range loop
      if s(i) = c then
        n := n + 1;
      end if;
    end loop;
    return n;
  end function;

  function sum_all(v : int_array) return integer is
    variable s : integer := 0;
  begin
    for i in v'range loop
      s := s + v(i);
    end loop;
    return s;
  end function;

  function doubled(v : int_array) return int_array is
    variable r : int_array(v'range);
  begin
    for i in v'range loop
      r(i) := 2 * v(i);
    end loop;
    return r;
  end function;

  procedure increment(v : inout int_array) is
  begin
    for i in v'range loop
      v(i) := v(i) + 1;
    end loop;
  end procedure;
begin
  process
    variable word : string(1 to 5) := "Hello";
    alias head : string(4 downto 1) is word(1 to 4);
    alias third : character is word(3);
    variable bits : bit_vector(0 to 7) := "00110101";
    alias middle : bit_vector(1 to 2) is bits(2 to 3);
    variable x : int_array(1 to 3) := (1, 2, 3);
    variable y : int_array(1 to 3);
    variable d : distance := 2 ft;
    variable failures : natural := 0;
  begin
    if head /= "Hell" or head(4) /= 'H' or head(1) /= 'l' or third /= 'l' then
      failures := failures + 1;
    end if;
    if middle /= "11" or middle(1) /= '1' then
      failures := failures + 1;
    end if;
    if count_of(word, 'l') /= 2 or count_of("banana", 'a') /= 3 then
      failures := failures + 1;
    end if;
    if sum_all(x) /= 6 then
      failures := failures + 1;
    end if;
    y := doubled(x);
    if y(1) /= 2 or y(3) /= 6 then
      failures := failures + 1;
    end if;
    increment(x);
    if x(1) /= 2 or x(3) /= 4 then
      failures := failures + 1;
    end if;
    if d /= 24000 mil then
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
