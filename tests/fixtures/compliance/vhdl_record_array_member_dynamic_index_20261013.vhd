-- fsim: top=record_array_member_dynamic_index(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 5.3.2.1 and 8.4: grouped multidimensional indexing of an
-- array-valued record element preserves its containing record fields.
entity record_array_member_dynamic_index is
end entity;

architecture sim of record_array_member_dynamic_index is
  type pair is array (1 to 2) of bit;
  type grid is array (integer range <>, boolean range <>) of pair;
  subtype small_grid is grid(1 to 3, false to true);
  type holder is record
    cells : small_grid;
    flag : bit;
  end record;
begin
  process
    variable h : holder;
    variable row_index : integer := 2;
    variable k : pair := "10";
    variable failures : natural := 0;
  begin
    h.flag := '1';
    h.cells(row_index, false) := k;
    if h.cells(2, false) /= k or h.flag /= '1' then
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
