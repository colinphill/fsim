-- SPDX-License-Identifier: Apache-2.0
--
-- The matching equality "?=" (IEEE 1076-2019 9.2.3, std_logic_1164) of wide
-- (65, 128, 200 and 256 element) std_logic_vectors holding all nine values,
-- in the v4 kernel (KOp::wide_alu, WideFamily::binary, vhdl_match_equal),
-- against an oracle that matches one element pair at a time and folds the
-- pairs with the std_logic "and" table:
--   - '-' in either element of a pair matches anything;
--   - 'L' and 'H' count as '0' and '1';
--   - a pair of known elements that differ gives '0', which decides the
--     fold whatever the other pairs give;
--   - otherwise a pair with a 'U' gives 'U' ('U' and X give 'U' in the
--     table), a pair with X, 'Z' or 'W' gives 'X', and all other pairs
--     give '1'.
-- Six constructed pairs have results derived by hand (see `special`): they
-- make the vector's pairs known and equal ('1'), X in one pair ('X'), 'U' in
-- one pair and X in another ('U'), a mismatch added to that ('0'), a '-'
-- hiding the X ('1') and a '-' hiding the 'U' beside an X ('X'). The expected
-- output (wide_match.expected) is that every width reports zero mismatches
-- and that all four results ('0' '1' 'U' 'X') were seen.
library ieee;
use ieee.std_logic_1164.all;

entity match_case is
  generic (W : positive);
end entity;

architecture sim of match_case is
  subtype vec is std_logic_vector(W - 1 downto 0);
  type levels is array (0 to 8) of std_logic;
  constant nine : levels := ('U', 'X', '0', '1', 'Z', 'W', 'L', 'H', '-');
  type results is array (0 to 5) of std_logic;
  -- The results of the six constructed pairs, in order.
  constant special : results := ('1', 'X', 'U', '0', '1', 'X');

  signal a, b : vec := (others => '0');
  signal r_match : std_logic;

  function make(seed : natural) return vec is
    variable v : vec;
  begin
    for i in v'range loop
      v(i) := nine((i * seed + seed / 2) mod 9);
    end loop;
    return v;
  end function;

  -- Only 0 and 1: L and H keep their meaning, every other element is 1 at
  -- even positions and 0 at odd ones.
  function boost(v : vec) return vec is
    variable r : vec;
  begin
    for i in v'range loop
      case v(i) is
        when '0' | 'L' => r(i) := '0';
        when '1' | 'H' => r(i) := '1';
        when others =>
          if (i mod 2) = 0 then r(i) := '1'; else r(i) := '0'; end if;
      end case;
    end loop;
    return r;
  end function;

  -- The same value with every third element written as its weak form.
  function weak_form(v : vec) return vec is
    variable r : vec := v;
  begin
    for i in v'range loop
      if (i mod 3) = 0 then
        if v(i) = '0' then r(i) := 'L';
        elsif v(i) = '1' then r(i) := 'H';
        end if;
      end if;
    end loop;
    return r;
  end function;

  -- "?=" of the vectors, one pair at a time.
  function oracle(x, y : vec) return std_logic is
    variable r : std_logic := '1';
  begin
    for i in x'range loop
      r := r and (x(i) ?= y(i));
    end loop;
    return r;
  end function;
begin
  r_match <= a ?= b;

  check : process
    variable bad : natural := 0;
    variable checks : natural := 0;
    variable seen : string(1 to 4) := "....";
    variable base : vec;

    procedure note(r : std_logic) is
    begin
      case r is
        when '0' => seen(1) := '0';
        when '1' => seen(2) := '1';
        when 'U' => seen(3) := 'U';
        when 'X' => seen(4) := 'X';
        when others => bad := bad + 1;
      end case;
    end procedure;
  begin
    for s in 1 to 9 loop
      for t in 1 to 9 loop
        a <= make(s);
        b <= make(t);
        wait for 1 ns;
        if r_match /= oracle(a, b) then bad := bad + 1; end if;
        note(r_match);
        checks := checks + 1;
      end loop;
    end loop;
    for k in special'range loop
      base := boost(make(k + 3));
      a <= base;
      case k is
        when 0 =>
          b <= weak_form(base);
        when 1 =>
          b <= base;
          b(3) <= 'X';
        when 2 =>
          b <= base;
          b(3) <= 'U';
          b(W - 1) <= 'X';
        when 3 =>
          b <= base;
          b(3) <= 'U';
          b(W - 1) <= 'X';
          b(5) <= not base(5);
        when 4 =>
          b <= base;
          b(3) <= '-';
        when others =>
          a(3) <= '-';
          b <= base;
          b(3) <= 'U';
          b(W - 1) <= 'X';
      end case;
      wait for 1 ns;
      if r_match /= oracle(a, b) then bad := bad + 1; end if;
      if r_match /= special(k) then bad := bad + 1; end if;
      note(r_match);
      checks := checks + 1;
    end loop;
    report "wide_match W=" & integer'image(W) & " checks=" & integer'image(checks)
      & " mismatches=" & integer'image(bad) & " outcomes=" & seen;
    wait;
  end process;
end architecture;

entity top is
end entity;

architecture sim of top is
begin
  w65 : entity work.match_case generic map (W => 65);
  w128 : entity work.match_case generic map (W => 128);
  w200 : entity work.match_case generic map (W => 200);
  w256 : entity work.match_case generic map (W => 256);
end architecture;
