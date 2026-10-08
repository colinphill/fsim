-- fsim: top=vhdl_real_arithmetic(sim) std=2008
-- IEEE 1076-2008 5.2.5 and 9.2: runtime REAL arithmetic, comparison,
-- negation, conversion, and images.
entity vhdl_real_arithmetic is
end entity;

architecture sim of vhdl_real_arithmetic is
  signal level : real := 0.0;

  function halve(x : real) return real is
  begin
    return x / 2.0;
  end function;
begin
  process
    variable x : real := -0.25;
    variable y : real;
    variable n : integer;
    variable failures : natural := 0;
  begin
    if not (x < 0.0) then failures := failures + 1; end if;
    y := x * 2.0;
    if y /= -0.5 then failures := failures + 1; end if;
    y := -x + 1.0;
    if y /= 1.25 then failures := failures + 1; end if;
    y := abs x;
    if y /= 0.25 then failures := failures + 1; end if;
    if halve(3.0) /= 1.5 then failures := failures + 1; end if;
    n := integer(2.6);
    if n /= 3 then failures := failures + 1; end if;
    y := real(7) / 2.0;
    if y /= 3.5 then failures := failures + 1; end if;
    level <= y;
    wait for 1 ns;
    if level <= 3.0 or level >= 4.0 then failures := failures + 1; end if;
    report "y=" & real'image(y);
    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
