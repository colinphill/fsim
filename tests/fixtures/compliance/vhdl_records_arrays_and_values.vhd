-- fsim: top=records_arrays_and_values(sim) std=2008
-- IEEE 1076-2008 8.4: an element of a two-dimensional array whose elements
-- are arrays, also as a record element selected by constant indices;
-- 6.4.2.2: elements of record constants; 16.2.2: 'VALUE of integer and
-- enumeration types; 10.6.2: assignment to an element of a STRING
-- variable.
entity records_arrays_and_values is
end entity;

architecture sim of records_arrays_and_values is
  type color is (red, green, blue);
  type pair is array (1 to 2) of bit;
  type grid is array (integer range <>, boolean range <>) of pair;
  subtype small_grid is grid(1 to 3, false to true);
  type holder is record
    flag : bit;
    cells : small_grid;
  end record;
  type point is record
    x : integer;
    y : integer;
  end record;
  constant origin : point := (3, 4);
begin
  process
    variable g : small_grid;
    variable h : holder;
    constant k : pair := "10";
    variable based : string(1 to 5) := "  -31";
    variable grouped : string(1 to 5) := "1000 ";
    variable shade : string(1 to 6) := " Blue ";
    variable truth : string(1 to 4) := "TRUE";
    variable word : string(1 to 3) := "abc";
    variable reversed : string(3 downto 1) := "xyz";
    variable failures : natural := 0;
  begin
    g(2, true) := k;
    if g(2, true) /= k then
      failures := failures + 1;
    end if;
    h.cells(1, false) := k;
    if h.cells(1, false) /= k then
      failures := failures + 1;
    end if;
    if origin.x /= 3 or origin.y /= 4 then
      failures := failures + 1;
    end if;
    if integer'value(based) /= -31 or integer'value(grouped) /= 1000 then
      failures := failures + 1;
    end if;
    if color'value(shade) /= blue or boolean'value(truth) /= true then
      failures := failures + 1;
    end if;
    word(2) := 'Q';
    reversed(1) := 'K';
    if word /= "aQc" or reversed /= "xyK" then
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
