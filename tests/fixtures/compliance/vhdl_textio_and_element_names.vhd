-- fsim: top=vhdl_textio_and_element_names(sim) std=2008
-- IEEE 1076-2008 16.4: TextIO WRITE of TIME with a unit, REAL with DIGITS,
-- enumeration and vector values, OUTPUT, DEALLOCATE of a LINE and LINE
-- formals of mode inout; 9.3.4: operator symbol function calls; 9.3.5:
-- qualified character literals; 16.2: attributes of indexed and selected
-- names; 14.7.3.2 and 11.3: 'EVENT and sensitivity of a signal element.
use std.textio.all;

entity vhdl_textio_and_element_names is
end entity;

architecture sim of vhdl_textio_and_element_names is
  type color is (red, green, blue);
  type rec is record
    x : bit_vector(1 to 3);
  end record;
  type row is array (1 to 2) of bit_vector(5 downto 2);

  signal vec : bit_vector(1 downto 0);
  signal wakeups : natural := 0;

  procedure tag(variable l : inout line; s : in string) is
  begin
    write(l, s);
  end procedure;
begin
  watch : process (vec(0)) is
  begin
    if now > 0 ns then
      wakeups <= wakeups + 1;
    end if;
  end process;

  drive : process is
  begin
    wait for 1 ns;
    vec <= "10";
    wait for 1 ns;
    vec <= "11";
    wait;
  end process;

  check : process is
    variable l : line;
    variable failures : natural := 0;
    variable r : rec;
    variable m : row;
    variable b : bit;
  begin
    write(l, 5 ns, right, 8, ns);
    if l.all /= "    5 ns" then failures := failures + 1; end if;
    deallocate(l);
    write(l, 1500 ns, left, 0, us);
    if l.all /= "1.5 us" then failures := failures + 1; end if;
    deallocate(l);
    write(l, 2.5, right, 0, 2);
    write(l, color'image(green));
    write(l, bit_vector'("1010"));
    if l.all /= "2.50green1010" then failures := failures + 1; end if;
    deallocate(l);
    tag(l, "ab");
    tag(l, "cd");
    if l.all /= "abcd" then failures := failures + 1; end if;
    deallocate(l);
    if "abs"(-4) /= 4 or not std.standard."<"(1, 2) then
      failures := failures + 1;
    end if;
    b := bit'('1');
    if b /= '1' then failures := failures + 1; end if;
    if r.x'length /= 3 or m(1)'left /= 5 then failures := failures + 1; end if;
    wait on vec;
    if vec(0)'event or not vec(1)'event then failures := failures + 1; end if;
    wait on vec;
    if not vec(0)'event or vec(1)'event then failures := failures + 1; end if;
    wait for 1 ns;
    if wakeups /= 1 then failures := failures + 1; end if;
    write(l, string'("textio "));
    write(l, time'image(1 ns));
    writeline(output, l);
    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
