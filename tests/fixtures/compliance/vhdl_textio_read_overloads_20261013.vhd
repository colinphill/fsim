-- fsim: top=vhdl_textio_read_overloads_20261013(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 16.4: TextIO READ overload behavior.
use std.textio.all;

entity vhdl_textio_read_overloads_20261013 is
end entity;

architecture sim of vhdl_textio_read_overloads_20261013 is
begin
  check : process is
    variable l : line;
    variable good : boolean;
    variable text_value : string(1 to 3);
    variable c : character;
    variable b : bit_vector(1 to 4) := "0000";
    variable boolean_value : boolean;
    variable integer_value : integer;
    variable r : real;
    variable failures : natural := 0;
  begin
    write(l, string'("  abc"));
    read(l, text_value, good);
    if not good or text_value /= "  a" or l.all /= "bc" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'(" xyz"));
    read(l, c, good);
    if not good or c /= ' ' or l.all /= "xyz" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("TRUE suffix"));
    read(l, boolean_value, good);
    if not good or not boolean_value or l.all /= " suffix" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("TRUEsuffix"));
    read(l, boolean_value, good);
    if good or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("  10_01tail"));
    read(l, b, good);
    if not good or b /= "1001" or l.all /= "tail" then
      failures := failures + 1;
    end if;
    deallocate(l);

    b := "0000";
    write(l, string'("1x"));
    read(l, b, good);
    if good or l.all /= "x" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("10"));
    read(l, b, good);
    if good or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("1994.5E+0 "));
    read(l, r, good);
    if not good or r /= 1994.5 or l.all /= " " then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("1.2E-3 suffix"));
    read(l, r, good);
    if not good or abs(r - 0.0012) > 1.0E-15 or l.all /= " suffix" then
      failures := failures + 1;
    end if;
    deallocate(l);

    -- An integer literal is a REAL value without a fraction (nvc textio7).
    write(l, string'("12"));
    read(l, r, good);
    if not good or r /= 12.0 or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("1.2E+"));
    read(l, r, good);
    if good or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("ABC"));
    read(l, integer_value, good);
    if good or l.all /= "ABC" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("12__3"));
    read(l, integer_value, good);
    if not good or integer_value /= 12 or l.all /= "__3" then
      failures := failures + 1;
    end if;
    deallocate(l);

    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
