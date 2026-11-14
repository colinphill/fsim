-- SPDX-License-Identifier: Apache-2.0
--
-- STD_ULOGIC values folded at elaboration keep all nine values (IEEE
-- 1076-2019 16.7, 5.2.2.1): a generic, constant or literal is the value at
-- its position U X 0 1 Z W L H -, not only '0' or '1'. Each generate
-- condition below holds, so every process reports; the leaf's generics are
-- also read at run time.
library ieee;
use ieee.std_logic_1164.all;

package sup_pkg is
  type strength_table is array (std_ulogic) of std_ulogic;
  constant forcing_of : strength_table :=
    ('U', 'X', '0', '1', 'X', 'X', '0', '1', 'X');
  function to_forcing (s : std_ulogic) return std_ulogic;
end package;

package body sup_pkg is
  function to_forcing (s : std_ulogic) return std_ulogic is
  begin
    return forcing_of(s);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;

entity sup_leaf is
  generic (g : std_ulogic := 'Z'; h : std_ulogic := '0');
end entity;

architecture a of sup_leaf is
  constant k : std_ulogic := 'L';
  constant t : std_ulogic_vector(0 to 2) := (g, h, k);
begin
  process
  begin
    report "supos leaf " & std_ulogic'image(g) & std_ulogic'image(h) & " "
      & boolean'image(g = 'Z') & " " & boolean'image(h = 'H') & " "
      & integer'image(std_ulogic'pos(h)) & " " & to_string(t) & " "
      & std_ulogic'image(to_x01(h));
    wait;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.sup_pkg.all;

entity top is
  generic (g : std_ulogic := 'Z'; h : std_ulogic := 'H';
           c : std_ulogic := '1'; z : std_ulogic := '0');
end entity;

architecture a of top is
  constant k : std_ulogic := 'L';
  constant v : std_ulogic_vector(0 to 1) := ('1', 'W');
begin
  g1 : if g = 'Z' generate
    process begin report "supos g1 g is Z"; wait; end process;
  end generate;
  g2 : if h /= '1' and c = '1' generate
    process begin report "supos g2 h is not 1"; wait; end process;
  end generate;
  g3 : if std_ulogic'pos(g) = 4 and k = 'L' generate
    process begin report "supos g3 pos of Z is 4"; wait; end process;
  end generate;
  g4 : if to_forcing(h) = '1' and to_forcing(k) = '0' and to_forcing(g) = 'X' generate
    process begin report "supos g4 table lookup"; wait; end process;
  end generate;
  g5 : if v(0) = '1' and v(1) = 'W' and v(1) /= z generate
    process begin report "supos g5 vector elements"; wait; end process;
  end generate;
  g6 : if std_ulogic'val(7) = h and std_ulogic'succ(c) = g generate
    process begin report "supos g6 val and succ"; wait; end process;
  end generate;
  g7 : case g generate
    when '0' | '1' =>
      process begin report "supos g7 wrong 01"; wait; end process;
    when 'Z' =>
      process begin report "supos g7 case Z"; wait; end process;
    when others =>
      process begin report "supos g7 wrong others"; wait; end process;
  end generate;
  u1 : entity work.sup_leaf generic map (h => 'H');
  u2 : entity work.sup_leaf generic map (g => '-', h => k);
end architecture;
