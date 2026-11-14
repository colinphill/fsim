-- SPDX-License-Identifier: Apache-2.0
--
-- The type mark of a qualified expression names a type or subtype (IEEE
-- 1076-2019 9.3.5, 6.3), and which one is decided where the mark is written
-- (12.3, 12.4). A subtype keeps its own constraint, which gives an OTHERS
-- aggregate its length (9.3.3.3); a subtype declared in an inner scope
-- hides an outer one of the same name; the subtype indication of an external
-- name (8.7) is resolved the same way, through a selected name that no use
-- clause makes visible.
package pkgsel is
  subtype byte is bit_vector(7 downto 0);
end package;

entity child is
end entity;

architecture a of child is
  signal s : bit_vector(7 downto 0) := "10100101";
begin
end architecture;

entity top is
end entity;

architecture a of top is
  subtype b4 is bit_vector(1 to 4);
  subtype byte is bit_vector(3 downto 0);
  constant c4 : bit_vector := b4'((others => '1'));
begin
  u1 : entity work.child;

  g1 : if c4'length = 4 generate
    process begin report "qmark outer b4 length 4"; wait; end process;
  end generate;

  inner : block
    subtype b4 is bit_vector(1 to 8);
    constant c8 : bit_vector := b4'((others => '1'));
  begin
    g2 : if c8'length = 8 generate
      process begin report "qmark inner b4 length 8"; wait; end process;
    end generate;
  end block;

  process
  begin
    wait for 1 ns;
    report "qmark runtime c4 length " & integer'image(c4'length);
    -- BYTE here is the architecture's 4-bit subtype; the external name's
    -- mark is the package's 8-bit one.
    report "qmark external " & to_string(<< signal .top.u1.s : work.pkgsel.byte >>);
    wait;
  end process;
end architecture;
