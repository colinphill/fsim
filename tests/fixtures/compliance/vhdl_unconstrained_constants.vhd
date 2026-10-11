-- fsim: top=vhdl_unconstrained_constants(a) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 5.3.2.2: an unconstrained constant takes its index range
-- from its value; a literal starts at the index subtype's left bound.
entity vhdl_unconstrained_constants is
end entity;

architecture a of vhdl_unconstrained_constants is
  type naturals is array (natural range <>) of integer;
  constant bits : bit_vector := B"111_111_110";
  constant text : string := "Hello";
  constant values : naturals := (10, 20, 30);
begin
  process
  begin
    assert bits'left = 0 and bits'right = 8 report "bits range" severity error;
    assert bits(0) = '1' and bits(7) = '1' and bits(8) = '0'
      report "bits elements" severity error;
    assert text(1) = 'H' and text(5) = 'o' report "text elements" severity error;
    assert values(0) = 10 and values(2) = 30 report "values elements" severity error;
    report "PASS";
    wait;
  end process;
end architecture;
