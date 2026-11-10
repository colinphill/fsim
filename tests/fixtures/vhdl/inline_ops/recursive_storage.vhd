-- SPDX-License-Identifier: Apache-2.0
--
-- Recursive subprograms whose activations own strings, arrays and access
-- values, in processes the v4 kernel compiles (IEEE 1076-2019 14.6 b),
-- 4.3: each call elaborates its declarative part anew, so every activation
-- has its own formals and locals however deep the recursion; 10.2: a wait
-- inside a procedure keeps the whole call stack). Every local is written
-- before the nested call and read after it, so an activation that sees a
-- deeper activation's value gives a different result.
--   tags(n): a local string(1 to 3) "<d>:<d>" with d = n mod 10, then
--          tags(n - 1) & the local: for n = 12 the 13 tags of 0 .. 12.
--   squares(n): a local integer_vector(0 to 2) (n, n*n, 1), then
--          squares(n - 1) + the three elements, squares(0) = 1: the sum of
--          n + n*n + 1 over 1 .. 300, plus 1, = 45150 + 9045050 + 300 + 1
--          = 9090501.
--   flips(v, i): a local bit_vector(v'range) (v an unconstrained formal)
--          holding v with bit i inverted, the nested call flips(v, i + 1),
--          then the local must differ from v in bit i only; the result
--          counts the '0' bits of v (0110100101 has 5).
--   ramp(n): a local string(1 to n + 1), its length set by the call, filled
--          with one letter, the nested call ramp(n - 1), then the local
--          must hold n + 1 of its letter: sum of n + 1 over 0 .. 30 = 496.
--   chain(n, acc): a local line holding integer'image(n), the nested call
--          chain(n - 1, acc), then acc gets the local's text and ','; for
--          n = 9 acc = "0,1,2,...,9," (a line formal of mode inout).
--   fill(n, arr): an inout integer_vector formal and a local copy of it
--          (arr'range) taken on entry; arr(n) := 3n before the nested
--          call, after it the copy must still hold the array before this
--          call (arr(n) = 0 there, else 100000 is added), and arr(n) gets
--          the copy's sum added: arr(k) = 3k + 3 (k + 1 + ... + 7), so
--          84,84,81,75,66,54,39,21 for k = 0 .. 7.
--   marks(n, text): an inout string formal and a local copy of it; the
--          call marks text(n + 1) with the letter n of the alphabet before
--          the nested call, after it the copy must hold '.' there (the
--          text before this call), else the mark becomes '!':
--          "ABCDEFGHIJ".
--   pause(n, acc): a local string(1 to 2) "p<d>", wait for 1 ps, the
--          nested call, then acc gets the local: for n = 39, 40 waits,
--          "p0p1...p9" four times, reported at 40 ps.
-- The other reports are at 1 ns.
library std;
use std.textio.all;

entity top is
end entity;

architecture sim of top is
  signal go : bit := '0';

  function tags(n : natural) return string is
    variable mine : string(1 to 3);
  begin
    mine := character'val(48 + n mod 10) & ':' & character'val(48 + n mod 10);
    if n = 0 then
      return mine;
    end if;
    return tags(n - 1) & mine;
  end function;

  function squares(n : natural) return integer is
    variable mine : integer_vector(0 to 2);
    variable below : integer;
  begin
    mine := (n, n * n, 1);
    if n = 0 then
      return 1;
    end if;
    below := squares(n - 1);
    return below + mine(0) + mine(1) + mine(2);
  end function;

  function flips(v : bit_vector; i : natural) return natural is
    variable mine : bit_vector(v'range);
    variable below : natural := 0;
    variable differ : natural := 0;
    variable zero : natural := 0;
  begin
    mine := v;
    mine(i) := not v(i);
    if i < v'high then
      below := flips(v, i + 1);
    end if;
    -- The whole local must still be this activation's: it differs from v
    -- in bit i only.
    for k in v'range loop
      if mine(k) /= v(k) then
        differ := differ + 1;
      end if;
    end loop;
    if differ /= 1 or mine(i) = v(i) then
      return 1000;
    end if;
    if v(i) = '0' then
      zero := 1;
    end if;
    return below + zero;
  end function;

  function ramp(n : natural) return natural is
    variable mine : string(1 to n + 1);
    variable below : natural := 0;
  begin
    for k in mine'range loop
      mine(k) := character'val(97 + n mod 26);
    end loop;
    if n > 0 then
      below := ramp(n - 1);
    end if;
    for k in mine'range loop
      if mine(k) /= character'val(97 + n mod 26) then
        return 100000;
      end if;
    end loop;
    return below + mine'length;
  end function;

  procedure marks(n : natural; text : inout string) is
    variable before : string(text'range);
  begin
    before := text;
    text(text'low + n) := character'val(65 + n);
    if n > 0 then
      marks(n - 1, text);
    end if;
    -- This activation's copy holds the text before its own mark.
    if before(text'low + n) /= '.' then
      text(text'low + n) := '!';
    end if;
  end procedure;

  procedure chain(n : natural; acc : inout line) is
    variable mine : line;
  begin
    write(mine, integer'image(n));
    if n > 0 then
      chain(n - 1, acc);
    end if;
    write(acc, mine.all & ",");
    deallocate(mine);
  end procedure;

  procedure fill(n : natural; arr : inout integer_vector) is
    variable before : integer_vector(arr'range);
    variable total : integer := 0;
  begin
    before := arr;
    arr(n) := n * 3;
    if n > 0 then
      fill(n - 1, arr);
    end if;
    for k in before'range loop
      total := total + before(k);
    end loop;
    if before(n) /= 0 then
      total := total + 100000;
    end if;
    arr(n) := arr(n) + total;
  end procedure;

  procedure pause(n : natural; acc : inout line) is
    variable mine : string(1 to 2);
  begin
    mine := 'p' & character'val(48 + n mod 10);
    wait for 1 ps;
    if n > 0 then
      pause(n - 1, acc);
    end if;
    write(acc, mine);
  end procedure;
begin
  go <= '1' after 1 ns;

  stringer : process (go)
  begin
    if go = '1' then
      report "wide tags=" & tags(12) & ";";
      report "wide squares=" & integer'image(squares(300)) & ";";
      report "wide flips=" & integer'image(flips("0110100101", 0)) & ";";
      report "wide ramp=" & integer'image(ramp(30)) & ";";
    end if;
  end process;

  liner : process (go)
    variable acc : line;
    variable arr : integer_vector(0 to 7);
    variable text : line;
    variable marked : string(1 to 10);
  begin
    if go = '1' then
      chain(9, acc);
      report "wide chain=" & acc.all & ";";
      deallocate(acc);
      arr := (others => 0);
      fill(7, arr);
      for k in arr'range loop
        write(text, integer'image(arr(k)) & ",");
      end loop;
      report "wide fill=" & text.all & ";";
      deallocate(text);
      marked := (others => '.');
      marks(9, marked);
      report "wide marks=" & marked & ";";
    end if;
  end process;

  pauser : process
    variable acc : line;
  begin
    pause(39, acc);
    report "wide pause=" & acc.all & " at " & time'image(now) & ";";
    wait;
  end process;
end architecture;
