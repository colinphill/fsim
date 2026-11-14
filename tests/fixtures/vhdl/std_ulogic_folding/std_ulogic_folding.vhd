-- SPDX-License-Identifier: Apache-2.0
--
-- STD_ULOGIC values folded at elaboration (IEEE 1076-2019 16.7, 9.4.3, 11.8):
-- the scalar subprograms of IEEE.STD_LOGIC_1164 are globally static, an
-- element of a constant given by a string literal is its character, and a
-- STD_ULOGIC generic actual that is another generic, an attribute or a
-- function reaches the child as the value at its position U X 0 1 Z W L H -.
-- Each generate condition below holds, so each process reports; the leaf
-- reports the generic it read at run time next to the value its conditions
-- folded.
library ieee;
use ieee.std_logic_1164.all;

entity r3_leaf is
  generic (g : std_ulogic := 'U');
end entity;

architecture a of r3_leaf is
begin
  -- The condition folds at elaboration; the report reads the generic at run
  -- time, so each instance checks the two against each other.
  l1 : if g = 'H' generate
    process begin report "r3fold leaf " & std_ulogic'image(g) & " folds H"; wait; end process;
  end generate;
  l2 : if g = 'Z' generate
    process begin report "r3fold leaf " & std_ulogic'image(g) & " folds Z"; wait; end process;
  end generate;
  l3 : if g = 'X' generate
    process begin report "r3fold leaf " & std_ulogic'image(g) & " folds X"; wait; end process;
  end generate;
  l4 : if g = 'W' generate
    process begin report "r3fold leaf " & std_ulogic'image(g) & " folds W"; wait; end process;
  end generate;
  l5 : if g = 'L' generate
    process begin report "r3fold leaf " & std_ulogic'image(g) & " folds L"; wait; end process;
  end generate;
  l6 : if g = '1' generate
    process begin report "r3fold leaf " & std_ulogic'image(g) & " folds 1"; wait; end process;
  end generate;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity r3_mid is
  generic (m : std_ulogic := 'H'; n : std_ulogic := '1');
end entity;

architecture a of r3_mid is
begin
  u1 : entity work.r3_leaf generic map (g => m);
  u2 : entity work.r3_leaf generic map (g => std_ulogic'succ(n));
  u3 : entity work.r3_leaf generic map (g => std_ulogic'val(std_ulogic'pos(n) + 4));
  u4 : entity work.r3_leaf generic map (g => to_x01(m));
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity top is
  generic (h : std_ulogic := 'H');
end entity;

architecture a of top is
  constant v : std_ulogic_vector := "1W";
  constant b : bit_vector := "10";
  constant w : std_ulogic_vector(3 downto 0) := "01HL";
  constant z : std_ulogic_vector(0 to 3) := "UZ-H";
  constant bv : bit_vector(2 downto 0) := "101";
  constant e : std_ulogic_vector(1 to 0) := "";
  constant re : std_ulogic_vector := to_x01(e);
  constant re2 : std_ulogic_vector := to_01(e);
  constant q : std_ulogic_vector := to_x01z(std_ulogic_vector'("01XZ"));
  constant fv : std_logic_vector(3 downto 0) := to_x01z(std_ulogic_vector'("1ZHL"));
  signal sb : bit := '1';
  signal sbv : bit_vector(2 downto 0) := "101";
begin
  g1 : if to_x01('H') = '1' generate
    process begin report "r3fold g1 to_x01 H"; wait; end process;
  end generate;
  g2 : if to_x01('Z') = 'X' generate
    process begin report "r3fold g2 to_x01 Z"; wait; end process;
  end generate;
  g3 : if to_01('L') = '0' generate
    process begin report "r3fold g3 to_01 L"; wait; end process;
  end generate;
  g4 : if is_x('W') and not is_x('H') generate
    process begin report "r3fold g4 is_x"; wait; end process;
  end generate;
  g5 : if to_ux01('U') = 'U' and to_x01z('Z') = 'Z' generate
    process begin report "r3fold g5 ux01 x01z"; wait; end process;
  end generate;
  g6 : if to_bit('H') = '1' generate
    process begin report "r3fold g6 to_bit H"; wait; end process;
  end generate;
  g7 : if to_x01(h) = '1' generate
    process begin report "r3fold g7 to_x01 generic"; wait; end process;
  end generate;
  g8 : if v(0) = '1' and v(1) = 'W' generate
    process begin report "r3fold g8 string literal elements"; wait; end process;
  end generate;
  g9 : if b(0) = '1' and b(1) = '0' generate
    process begin report "r3fold g9 bit_vector elements"; wait; end process;
  end generate;
  g10 : if w(3) = '0' and w(2) = '1' and w(1) = 'H' and w(0) = 'L' generate
    process begin report "r3fold g10 constrained elements"; wait; end process;
  end generate;
  g11 : case to_x01(h) generate
    when '1' =>
      process begin report "r3fold g11 case to_x01"; wait; end process;
    when others =>
      process begin report "r3fold g11 wrong"; wait; end process;
  end generate;
  g12 : if to_x01(w)(1) = '0' and to_x01(w)(2) = '1' and to_x01(w)(3) = '1' and to_x01(w)(4) = '0' generate
    process begin report "r3fold g12 to_x01 vector bounds 1 to length"; wait; end process;
  end generate;
  g13 : if to_x01z(z)(1) = 'X' and to_x01z(z)(2) = 'Z' and to_x01z(z)(3) = 'X' and to_x01z(z)(4) = '1' generate
    process begin report "r3fold g13 to_x01z vector"; wait; end process;
  end generate;
  g14 : if to_ux01(z)(1) = 'U' and to_ux01(z)(2) = 'X' and to_ux01(z)(4) = '1' generate
    process begin report "r3fold g14 to_ux01 vector"; wait; end process;
  end generate;
  g15 : if to_x01(bv)(1) = '1' and to_x01(bv)(2) = '0' and to_x01(bv)(3) = '1' generate
    process begin report "r3fold g15 to_x01 bit_vector"; wait; end process;
  end generate;
  g16 : if to_01(w)(3) = '0' and to_01(w)(2) = '1' and to_01(w)(1) = '1' and to_01(w)(0) = '0' generate
    process begin report "r3fold g16 to_01 vector bounds length-1 downto 0"; wait; end process;
  end generate;
  g17 : if to_01(z)(3) = '0' and to_01(z)(0) = '0' and to_01(z, '1')(3) = '1' and to_01(z, 'X')(0) = 'X' generate
    process begin report "r3fold g17 to_01 bad element fills with xmap"; wait; end process;
  end generate;
  g18 : if to_bitvector(w)(3) = '0' and to_bitvector(w)(2) = '1' and to_bitvector(w)(0) = '0' generate
    process begin report "r3fold g18 to_bitvector strong"; wait; end process;
  end generate;
  g19 : if to_bitvector(z)(3) = '0' and to_bitvector(z)(0) = '1' and to_bitvector(z, '1')(3) = '1' generate
    process begin report "r3fold g19 to_bitvector xmap"; wait; end process;
  end generate;
  g20 : if to_stdulogicvector(bv)(2) = '1' and to_stdulogicvector(bv)(1) = '0' and to_stdulogicvector(bv)(0) = '1' generate
    process begin report "r3fold g20 to_stdulogicvector"; wait; end process;
  end generate;
  g21 : if to_stdlogicvector(w)(1) = 'H' and to_stdlogicvector(w)(0) = 'L' generate
    process begin report "r3fold g21 to_stdlogicvector"; wait; end process;
  end generate;
  g22 : if is_x(z) and not is_x(w) and is_x(to_x01(z)) and not is_x(to_x01(w)) generate
    process begin report "r3fold g22 is_x vector"; wait; end process;
  end generate;
  g23 : if to_x01(std_ulogic_vector'("01XZ"))(1) = '0' and to_x01(std_ulogic_vector'("01XZ"))(2) = '1' and to_x01(std_ulogic_vector'("01XZ"))(3) = 'X' and to_x01(std_ulogic_vector'("01XZ"))(4) = 'X' generate
    process begin report "r3fold g23 to_x01 qualified string literal"; wait; end process;
  end generate;
  g24 : if to_ux01(std_ulogic_vector'("U1Z-"))(1) = 'U' and to_ux01(std_ulogic_vector'("U1Z-"))(2) = '1' and to_ux01(std_ulogic_vector'("U1Z-"))(3) = 'X' and to_ux01(std_ulogic_vector'("U1Z-"))(4) = 'X' generate
    process begin report "r3fold g24 to_ux01 qualified string literal"; wait; end process;
  end generate;
  g25 : if is_x(std_ulogic_vector'("01XZ")) and not is_x(std_ulogic_vector'("01HL")) generate
    process begin report "r3fold g25 is_x qualified string literal"; wait; end process;
  end generate;
  g26 : if to_01(std_ulogic_vector'("01XZ"), '1')(3) = '1' and to_01(std_ulogic_vector'("01XZ"), '1')(0) = '1' and to_01(std_ulogic_vector'("0HL1"))(2) = '1' and to_01(std_ulogic_vector'("0HL1"))(1) = '0' generate
    process begin report "r3fold g26 to_01 qualified string literal"; wait; end process;
  end generate;
  g27 : if to_bitvector(std_ulogic_vector'("01XZ"))(2) = '1' and to_bitvector(std_ulogic_vector'("01XZ"))(1) = '0' and to_bitvector(std_ulogic_vector'("01XZ"), '1')(0) = '1' generate
    process begin report "r3fold g27 to_bitvector qualified string literal"; wait; end process;
  end generate;
  g28 : if re'length = 0 and re'left = 1 and re'right = 0 generate
    process begin report "r3fold g28 null to_x01 bounds 1 to 0"; wait; end process;
  end generate;
  g29 : if re2'length = 0 and re2'left = -1 and re2'right = 0 generate
    process begin report "r3fold g29 null to_01 bounds -1 downto 0"; wait; end process;
  end generate;
  g30 : if not is_x(e) generate
    process begin report "r3fold g30 is_x of a null vector"; wait; end process;
  end generate;
  g31 : if q(1) = '0' and q(2) = '1' and q(3) = 'X' and q(4) = 'Z' generate
    process begin report "r3fold g31 constant of a qualified literal"; wait; end process;
  end generate;
  -- The BIT and BIT_VECTOR overloads of the mapping functions with run-time
  -- operands (IEEE.STD_LOGIC_1164): STD_ULOGIC results of the same spelling.
  rt : process
    variable r : std_ulogic;
    variable rv : std_ulogic_vector(2 downto 0);
  begin
    wait for 1 ns;
    r := to_x01(sb);
    report "r3fold run to_x01 bit " & std_ulogic'image(r);
    r := to_01(sb);
    report "r3fold run to_01 bit " & std_ulogic'image(r);
    rv := to_x01z(sbv);
    report "r3fold run to_x01z bit_vector " & to_string(rv);
    rv := to_ux01(sbv);
    report "r3fold run to_ux01 bit_vector " & to_string(rv);
    rv := to_01(sbv);
    report "r3fold run to_01 bit_vector " & to_string(rv);
    -- Elements of a folded constant are read in a process, 'Z' and 'X'
    -- included.
    report "r3fold run elements " & std_logic'image(fv(3)) & std_logic'image(fv(2))
      & std_logic'image(fv(1)) & std_logic'image(fv(0));
    wait;
  end process;
  t1 : entity work.r3_mid generic map (m => h);
  t2 : entity work.r3_mid generic map (m => 'W', n => '0');
end architecture;
