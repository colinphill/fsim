-- fsim: top=tables_cases_and_subtypes(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 10.9: case choices given by character literals of a BIT
-- selector; 9.3.3: a two-dimensional constant table indexed by character
-- literals; 16.2.3: 'LENGTH, 'LEFT and 'RIGHT of a STRING variable whose
-- bounds come from a subprogram formal; 16.2.2: 'SUBTYPE of an object as
-- a subtype indication.
entity tables_cases_and_subtypes is
end entity;

architecture sim of tables_cases_and_subtypes is
  type bit_table is array (bit, bit) of bit;
  constant and_table : bit_table := (('0', '0'), ('0', '1'));

  function bounds(constant size : in integer) return integer is
    variable result : string(1 to size);
  begin
    return result'length * 100 + result'left * 10 + result'right;
  end function;
begin
  process
    variable sel : bit := '1';
    variable k : integer := 0;
    variable word : bit_vector(3 downto 0) := "1010";
    variable copy : word'subtype;
    variable failures : natural := 0;
  begin
    case sel is
      when '0' => k := 1;
      when '1' => k := 2;
    end case;
    if k /= 2 then
      failures := failures + 1;
    end if;
    if and_table('1', '1') /= '1' or and_table('1', '0') /= '0' then
      failures := failures + 1;
    end if;
    if bounds(3) /= 313 then
      failures := failures + 1;
    end if;
    copy := word;
    if copy'length /= 4 or copy'left /= 3 or copy(3) /= '1' then
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
