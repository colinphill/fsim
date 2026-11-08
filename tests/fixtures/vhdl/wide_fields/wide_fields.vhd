-- SPDX-License-Identifier: Apache-2.0
--
-- Slice and element reads and writes of wide (65, 128, 129 and 200 element)
-- std_logic_vectors with all nine std_logic values among the elements, in the
-- v4 kernel (KOp::wide_alu extract, insert and dynamic_insert): in a process
-- (variables are registers) and in combinational members (concurrent calls of
-- functions whose locals are registers), against oracles that compare one
-- element at a time. The expected output (wide_fields.expected) is that every
-- width reports zero mismatches. The oracles follow IEEE 1076-2019:
--   8.5    an assignment to a slice name or an indexed name replaces exactly
--          the selected elements of the variable and leaves the others;
--   8.4    a slice or an indexed name reads the selected elements unchanged
--          ('U', 'X', '0', '1', 'Z', 'W', 'L', 'H' and '-' are distinct
--          values and "/=" tells them apart);
--   8.4    an index outside the index range is an error, so the dynamic
--          indexes stay inside the range.
-- Per width the process makes 6 seeds x 12 slice checks (six shapes, each in
-- the process and in a member) plus 6 seeds x 6 indexes x 9 values x 2 element
-- checks (process and member): 72 + 648 = 720 checks.
library ieee;
use ieee.std_logic_1164.all;

entity fields_case is
  generic (W : positive);
end entity;

architecture sim of fields_case is
  subtype vec is std_logic_vector(W - 1 downto 0);
  subtype s5_t is std_logic_vector(4 downto 0);
  subtype s65_t is std_logic_vector(64 downto 0);
  type levels is array (0 to 8) of std_logic;
  constant nine : levels := ('U', 'X', '0', '1', 'Z', 'W', 'L', 'H', '-');
  type indexes is array (0 to 5) of natural;
  constant picks : indexes := (0, 1, 63, 64, W - 1, W / 2);

  signal a_s, b_s : vec := (others => '0');
  signal k_s : natural := 0;
  signal e_s : std_logic := '0';
  signal m_low, m_top, m_wide_top, m_wide_low, m_bit : vec;
  signal m_get_top, m_get_low : s65_t;

  function make(seed : natural) return vec is
    variable v : vec;
  begin
    for i in v'range loop
      v(i) := nine((i * seed + seed / 2) mod 9);
    end loop;
    return v;
  end function;

  function set_low(t : vec; s : s5_t) return vec is
    variable r : vec;
  begin
    r := t;
    r(7 downto 3) := s;
    return r;
  end function;

  function set_top(t : vec; s : s5_t) return vec is
    variable r : vec;
  begin
    r := t;
    r(W - 1 downto W - 5) := s;
    return r;
  end function;

  function set_wide_top(t : vec; s : s65_t) return vec is
    variable r : vec;
  begin
    r := t;
    r(W - 1 downto W - 65) := s;
    return r;
  end function;

  function set_wide_low(t : vec; s : s65_t) return vec is
    variable r : vec;
  begin
    r := t;
    r(64 downto 0) := s;
    return r;
  end function;

  function get_top(t : vec) return s65_t is
  begin
    return t(W - 1 downto W - 65);
  end function;

  function get_low(t : vec) return s65_t is
  begin
    return t(64 downto 0);
  end function;

  function set_bit(t : vec; i : natural; e : std_logic) return vec is
    variable r : vec;
  begin
    r := t;
    r(i) := e;
    return r;
  end function;
begin
  m_low <= set_low(a_s, b_s(4 downto 0));
  m_top <= set_top(a_s, b_s(4 downto 0));
  m_wide_top <= set_wide_top(a_s, b_s(64 downto 0));
  m_wide_low <= set_wide_low(a_s, b_s(64 downto 0));
  m_get_top <= get_top(a_s);
  m_get_low <= get_low(a_s);
  m_bit <= set_bit(a_s, k_s, e_s);

  process
    variable bad, checks : natural := 0;
    variable a, b, v : vec;
    variable g : s65_t;
    variable expect : std_logic;
  begin
    for seed in 1 to 6 loop
      a := make(seed);
      b := make(seed + 3);
      a_s <= a;
      b_s <= b;
      wait for 1 ns;
      -- set_low: elements 7 downto 3 come from b(4 downto 0).
      v := a;
      v(7 downto 3) := b(4 downto 0);
      for i in 0 to W - 1 loop
        if i >= 3 and i <= 7 then expect := b(i - 3); else expect := a(i); end if;
        if v(i) /= expect then bad := bad + 1; end if;
        if m_low(i) /= expect then bad := bad + 1; end if;
      end loop;
      checks := checks + 2;
      -- set_top: the top five elements come from b(4 downto 0).
      v := a;
      v(W - 1 downto W - 5) := b(4 downto 0);
      for i in 0 to W - 1 loop
        if i >= W - 5 then expect := b(i - (W - 5)); else expect := a(i); end if;
        if v(i) /= expect then bad := bad + 1; end if;
        if m_top(i) /= expect then bad := bad + 1; end if;
      end loop;
      checks := checks + 2;
      -- set_wide_top: 65 elements from b(64 downto 0) at the top.
      v := a;
      v(W - 1 downto W - 65) := b(64 downto 0);
      for i in 0 to W - 1 loop
        if i >= W - 65 then expect := b(i - (W - 65)); else expect := a(i); end if;
        if v(i) /= expect then bad := bad + 1; end if;
        if m_wide_top(i) /= expect then bad := bad + 1; end if;
      end loop;
      checks := checks + 2;
      -- set_wide_low: 65 elements from b(64 downto 0) at the bottom.
      v := a;
      v(64 downto 0) := b(64 downto 0);
      for i in 0 to W - 1 loop
        if i <= 64 then expect := b(i); else expect := a(i); end if;
        if v(i) /= expect then bad := bad + 1; end if;
        if m_wide_low(i) /= expect then bad := bad + 1; end if;
      end loop;
      checks := checks + 2;
      -- get_top and get_low: slices of a.
      g := a(W - 1 downto W - 65);
      for j in 0 to 64 loop
        if g(j) /= a(W - 65 + j) then bad := bad + 1; end if;
        if m_get_top(j) /= a(W - 65 + j) then bad := bad + 1; end if;
      end loop;
      checks := checks + 2;
      g := a(64 downto 0);
      for j in 0 to 64 loop
        if g(j) /= a(j) then bad := bad + 1; end if;
        if m_get_low(j) /= a(j) then bad := bad + 1; end if;
      end loop;
      checks := checks + 2;
      -- Element writes at run-time indexes, all nine values.
      for p in picks'range loop
        for n in nine'range loop
          v := a;
          v(picks(p)) := nine(n);
          k_s <= picks(p);
          e_s <= nine(n);
          wait for 1 ns;
          for i in 0 to W - 1 loop
            if i = picks(p) then expect := nine(n); else expect := a(i); end if;
            if v(i) /= expect then bad := bad + 1; end if;
            if m_bit(i) /= expect then bad := bad + 1; end if;
          end loop;
          checks := checks + 2;
        end loop;
      end loop;
    end loop;
    report "wide_fields W=" & integer'image(W) & " checks=" & integer'image(checks)
      & " mismatches=" & integer'image(bad);
    wait;
  end process;
end architecture;

entity top is
end entity;

architecture sim of top is
begin
  w65 : entity work.fields_case generic map (W => 65);
  w128 : entity work.fields_case generic map (W => 128);
  w129 : entity work.fields_case generic map (W => 129);
  w200 : entity work.fields_case generic map (W => 200);
end architecture;
