-- fsim: top=b57_textio_ho_codecs(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 16.4: HREAD/OREAD consume valid radix prefixes even when
-- GOOD is false; an invalid next character remains on LINE.
use std.textio.all;

entity b57_textio_ho_codecs is
end entity;

architecture sim of b57_textio_ho_codecs is
begin
  check : process is
    variable l : line;
    variable good : boolean;
    variable bv4 : bit_vector(3 downto 0);
    variable bv6 : bit_vector(5 downto 0);
    variable bv8 : bit_vector(7 downto 0);
    variable bv11 : bit_vector(10 downto 0);
    variable bv13 : bit_vector(12 downto 0);
    variable bv16 : bit_vector(15 downto 0);
    variable failures : natural := 0;
  begin
    write(l, string'("0ABC"));
    hread(l, bv13, good);
    if not good or bv13 /= "0101010111100" or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("821"));
    hread(l, bv11, good);
    if good or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("A"));
    hread(l, bv8, good);
    if good or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("12_34tail"));
    hread(l, bv16, good);
    if not good or bv16 /= X"1234" or l.all /= "tail" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("a"));
    hex_read(l, bv4, good);
    if not good or bv4 /= "1010" or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("A_F"));
    hread(l, bv4, good);
    if not good or bv4 /= "1010" or l.all /= "_F" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("_A"));
    hread(l, bv4, good);
    if good or l.all /= "_A" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("1__2"));
    hread(l, bv8, good);
    if good or l.all /= "_2" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("  G1"));
    hread(l, bv8, good);
    if good or l.all /= "G1" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("G1"));
    hread(l, bv8, good);
    if good or l.all /= "G1" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("5_2next"));
    oread(l, bv6, good);
    if not good or bv6 /= "101010" or l.all /= "next" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("07"));
    octal_read(l, bv4, good);
    if not good or bv4 /= "0111" or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("20"));
    oread(l, bv4, good);
    if good or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    write(l, string'("2"));
    oread(l, bv6, good);
    if good or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    hwrite(l, "1010", field => 5);
    if l.all /= "    A" then
      failures := failures + 1;
    end if;
    deallocate(l);

    bv4 := "1010";
    hwrite(l, "1010");
    if l.all /= "A" then
      failures := failures + 1;
    end if;
    deallocate(l);

    hwrite(l, bv4);
    if l.all /= "A" then
      failures := failures + 1;
    end if;
    deallocate(l);

    hwrite(l, X"abc", field => 5, justified => left);
    if l.all /= "ABC  " then
      failures := failures + 1;
    end if;
    deallocate(l);

    hex_write(l, "1100");
    if l.all /= "C" then
      failures := failures + 1;
    end if;
    deallocate(l);

    bv8 := "00000001";
    hwrite(l, bv8);
    if l.all /= "01" then
      failures := failures + 1;
    end if;
    hread(l, bv8, good);
    if not good or bv8 /= "00000001" or l.all /= "" then
      failures := failures + 1;
    end if;
    deallocate(l);

    owrite(l, "111111");
    if l.all /= "77" then
      failures := failures + 1;
    end if;
    deallocate(l);

    owrite(l, "101", field => 4, justified => left);
    if l.all /= "5   " then
      failures := failures + 1;
    end if;
    deallocate(l);

    octal_write(l, "010");
    if l.all /= "2" then
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
