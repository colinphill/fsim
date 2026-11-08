-- SPDX-License-Identifier: Apache-2.0
--
-- Logic and shifts on wide (65, 128, 200 and 256 element) std_logic_vectors
-- and bit_vectors in the v4 kernel (KOp::wide_alu), with all nine std_logic
-- values among the elements, against oracles written element by element on
-- one-element values. The expected output (wide_alu.expected) is that every
-- width reports zero mismatches. The oracles follow IEEE 1076-2019:
--   9.2.2  the logical operators on arrays apply element by element, with the
--          std_logic_1164 tables for std_logic ('U' wins over anything but a
--          deciding 0 (and) or 1 (or); 'L' acts as 0, 'H' as 1, and the
--          results are 'U', 'X', '0' or '1' only);
--   9.2.3  "=" and "/=" on arrays compare element by element with the
--          predefined enumeration equality ('H' /= '1');
--   9.2.3  "sll", "srl", "sla", "sra", "rol" and "ror" move a bit_vector by
--          the count; a negative count moves the other way; "sla" fills with
--          the rightmost element and "sra" with the leftmost, a shift by the
--          length or more fills the whole value, a rotation is by the count
--          modulo the length (IEEE 1076-2019 9.2.3);
--   9.5    a conditional signal assignment selects whole values.
library ieee;
use ieee.std_logic_1164.all;

entity wide_case is
  generic (W : positive);
end entity;

architecture sim of wide_case is
  subtype vec is std_logic_vector(W - 1 downto 0);
  subtype bits is bit_vector(W - 1 downto 0);
  type levels is array (0 to 8) of std_logic;
  constant nine : levels := ('U', 'X', '0', '1', 'Z', 'W', 'L', 'H', '-');

  signal a, b : vec := (others => '0');
  signal pick : boolean := false;
  signal r_and, r_or, r_xor, r_not, r_nand, r_nor, r_xnor, r_sel : vec;
  signal r_eq, r_ne : boolean;

  signal bv : bits := (others => '0');
  signal n : integer := 0;
  signal r_sll, r_srl, r_sla, r_sra, r_rol, r_ror : bits;

  function make(seed : natural) return vec is
    variable v : vec;
  begin
    for i in v'range loop
      v(i) := nine((i * seed + seed / 2) mod 9);
    end loop;
    return v;
  end function;

  function make_bits(seed : natural) return bits is
    variable v : bits;
  begin
    for i in v'range loop
      if ((i * seed + seed) mod 5) < 2 then
        v(i) := '1';
      else
        v(i) := '0';
      end if;
    end loop;
    return v;
  end function;

  -- Element i of a shift of v by the signed count c; kind 0 sll, 1 srl,
  -- 2 sla, 3 sra, 4 rol, 5 ror.
  function moved(v : bits; c : integer; kind : natural; i : natural) return bit is
    variable k : natural := kind;
    variable m : integer := c;
    variable src : integer;
    variable len : integer := W;
  begin
    if m < 0 then
      m := -m;
      -- The other direction.
      case kind is
        when 0 => k := 1;
        when 1 => k := 0;
        when 2 => k := 3;
        when 3 => k := 2;
        when 4 => k := 5;
        when others => k := 4;
      end case;
    end if;
    case k is
      when 0 | 2 =>
        src := i - m;
        if src >= 0 then return v(src);
        elsif k = 2 then return v(0);
        else return '0';
        end if;
      when 1 | 3 =>
        src := i + m;
        if src < len then return v(src);
        elsif k = 3 then return v(len - 1);
        else return '0';
        end if;
      when 4 =>
        src := (i - m) mod len;
        return v(src);
      when others =>
        src := (i + m) mod len;
        return v(src);
    end case;
  end function;
begin
  r_and <= a and b;
  r_or <= a or b;
  r_xor <= a xor b;
  r_not <= not a;
  r_nand <= a nand b;
  r_nor <= a nor b;
  r_xnor <= a xnor b;
  r_eq <= a = b;
  r_ne <= a /= b;
  r_sel <= a when pick else b;

  r_sll <= bv sll n;
  r_srl <= bv srl n;
  r_sla <= bv sla n;
  r_sra <= bv sra n;
  r_rol <= bv rol n;
  r_ror <= bv ror n;

  check : process
    variable bad : natural := 0;
    variable checks : natural := 0;
    variable equal : boolean;
    type counts is array (0 to 8) of integer;
    constant amounts : counts := (-3, 0, 1, 63, 64, 65, W - 1, W, W + 5);
  begin
    for s in 1 to 9 loop
      for t in 1 to 9 loop
        a <= make(s);
        b <= make(t);
        pick <= (s mod 2) = 0;
        wait for 1 ns;
        equal := true;
        for i in 0 to W - 1 loop
          if r_and(i) /= (a(i) and b(i)) then bad := bad + 1; end if;
          if r_or(i) /= (a(i) or b(i)) then bad := bad + 1; end if;
          if r_xor(i) /= (a(i) xor b(i)) then bad := bad + 1; end if;
          if r_not(i) /= (not a(i)) then bad := bad + 1; end if;
          if r_nand(i) /= (a(i) nand b(i)) then bad := bad + 1; end if;
          if r_nor(i) /= (a(i) nor b(i)) then bad := bad + 1; end if;
          if r_xnor(i) /= (a(i) xnor b(i)) then bad := bad + 1; end if;
          if pick then
            if r_sel(i) /= a(i) then bad := bad + 1; end if;
          else
            if r_sel(i) /= b(i) then bad := bad + 1; end if;
          end if;
          if a(i) /= b(i) then equal := false; end if;
        end loop;
        if r_eq /= equal then bad := bad + 1; end if;
        if r_ne = equal then bad := bad + 1; end if;
        checks := checks + 1;
      end loop;
    end loop;
    for s in 1 to 5 loop
      for k in amounts'range loop
        bv <= make_bits(s);
        n <= amounts(k);
        wait for 1 ns;
        for i in 0 to W - 1 loop
          if r_sll(i) /= moved(bv, n, 0, i) then bad := bad + 1; end if;
          if r_srl(i) /= moved(bv, n, 1, i) then bad := bad + 1; end if;
          if r_sla(i) /= moved(bv, n, 2, i) then bad := bad + 1; end if;
          if r_sra(i) /= moved(bv, n, 3, i) then bad := bad + 1; end if;
          if r_rol(i) /= moved(bv, n, 4, i) then bad := bad + 1; end if;
          if r_ror(i) /= moved(bv, n, 5, i) then bad := bad + 1; end if;
        end loop;
        checks := checks + 1;
      end loop;
    end loop;
    report "wide_alu W=" & integer'image(W) & " checks=" & integer'image(checks)
      & " mismatches=" & integer'image(bad);
    wait;
  end process;
end architecture;

entity top is
end entity;

architecture sim of top is
begin
  w65 : entity work.wide_case generic map (W => 65);
  w128 : entity work.wide_case generic map (W => 128);
  w200 : entity work.wide_case generic map (W => 200);
  w256 : entity work.wide_case generic map (W => 256);
end architecture;
