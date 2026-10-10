-- fsim: top=vhdl_attributes_and_conversions(sim) std=1993
-- IEEE 1076-2008 6.7 and 7.2: user-defined attribute values; 6.5.7.1:
-- conversion functions in the formal part of an output port association;
-- 9.4: static REAL and CHARACTER array initial values; 9.2: a user-defined
-- "abs" is called rather than folded as the predefined operator.
entity conversion_source is
  port (
    o : out integer := 4
  );
end entity;

architecture rtl of conversion_source is
begin
  process
  begin
    wait for 1 ns;
    o <= 9;
    wait;
  end process;
end architecture;

entity vhdl_attributes_and_conversions is
end entity;

architecture sim of vhdl_attributes_and_conversions is
  type real_pair is array (1 to 2) of real;
  type char_grid is array (1 to 2, 1 to 3) of character;
  type level is (low, middle, high);

  constant scale : real := 2.5;
  constant halves : real_pair := (others => scale * 2.0);
  constant letters : char_grid := (others => (others => 'q'));

  function to_pair(value : integer) return real_pair is
  begin
    return (real(value), real(value) / 2.0);
  end function;

  function "abs" (value : integer) return level is
  begin
    if value < 0 then
      return low;
    elsif value = 0 then
      return middle;
    else
      return high;
    end if;
  end function;

  attribute width : integer;
  attribute width of halves : constant is 2;

  component conversion_source
    port (
      o : out integer
    );
  end component;

  signal converted : real_pair;
  signal initial : real_pair := halves;
  signal grid : char_grid := letters;
begin
  source : conversion_source
    port map (
      to_pair(o) => converted
    );

  process
    variable failures : natural := 0;
    variable sign : level;
  begin
    if halves'width /= 2 then failures := failures + 1; end if;
    if initial(1) /= 5.0 or initial(2) /= 5.0 then failures := failures + 1; end if;
    if grid(2, 3) /= 'q' then failures := failures + 1; end if;
    sign := abs 7;
    if sign /= high then failures := failures + 1; end if;
    wait for 2 ns;
    if converted(1) /= 9.0 or converted(2) /= 4.5 then
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
