-- fsim: top=element_constraints(sim) std=2008
-- IEEE 1076-2008 5.3.2.1 and 5.3.3.1: array and record element
-- constraints, including `open` index constraints, constraints through a
-- named subtype and nested record elements of an array; 6.4.2.3: default
-- values of constrained elements; 16.3: indexing, aggregates and defaults
-- of the predefined INTEGER_VECTOR and BOOLEAN_VECTOR.
library ieee;
use ieee.std_logic_1164.all;

entity element_constraints is
end entity;

architecture sim of element_constraints is
  type rec is record
    x : bit_vector;
    n : integer;
  end record;
  type slv_array is array (natural range <>) of std_logic_vector;
  subtype byte_array is slv_array(open)(7 downto 0);
  type rec_vec is array (natural range <>) of rec;

  signal s : rec(x(1 to 3));
  signal b : byte_array(0 to 1);
  signal m : slv_array(1 to 2)(3 downto 0) := (others => (others => '0'));
  signal rv : rec_vec(1 to 2)(x(0 to 1));
  signal iv : integer_vector(0 to 3);
begin
  process
    variable v : rec(x(1 to 3));
    variable w : integer_vector(1 to 2) := (5, 6);
    variable f : boolean_vector(1 to 2);
    variable failures : natural := 0;
  begin
    if s.x /= "000" or s.n /= integer'left or s.x'length /= 3 then
      failures := failures + 1;
    end if;
    v := (x => "101", n => 2);
    if v.x(2) /= '0' or v /= (x => "101", n => 2) then
      failures := failures + 1;
    end if;
    if w(2) /= 6 or iv(0) /= integer'left or f(2) then
      failures := failures + 1;
    end if;
    s.x <= "101";
    b(1) <= x"A5";
    m(2) <= "1100";
    rv(2).x <= "11";
    iv(1) <= 7;
    wait for 1 ns;
    if s.x /= "101" or b(1) /= x"A5" or b(0) /= "UUUUUUUU" then
      failures := failures + 1;
    end if;
    if m(2) /= "1100" or m(1) /= "0000" or rv(2).x /= "11"
      or rv(1).x /= "00" then
      failures := failures + 1;
    end if;
    if iv(1) /= 7 or iv(2) /= integer'left then
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
