-- SPDX-License-Identifier: Apache-2.0
-- A signal parameter passed on by the body of another subprogram, whole or
-- as an element or a slice whose indexes are static expressions (the actual
-- of a signal parameter is a static signal name, 4.2.2.3), denotes (part of)
-- the signal the enclosing call was given (IEEE 1076-2019 4.2.2.3); the formal of the nested call has the
-- drivers the calling process holds for it as its local contributors, or for
-- a formal of mode in those of the signal it denotes (19.12.2.2 c).
entity top is
end entity;

architecture rtl of top is
  signal sv : bit_vector(3 downto 0);
begin
  main : process is
    procedure whole(signal y : out bit_vector) is
    begin
      y <= "1111";
    end procedure;
    procedure elem(signal y : out bit) is
    begin
      y <= '1';
    end procedure;
    procedure part(signal y : out bit_vector) is
    begin
      y <= "11";
    end procedure;
    function peek(signal z : in bit) return bit is
    begin
      return z;
    end function;
    function twice(n : integer) return integer is
    begin
      return n * 2;
    end function;
    procedure outer(signal x : inout bit_vector(3 downto 0)) is
      variable v : bit;
      constant c : integer := 1;
    begin
      whole(x);
      elem(x(2));
      part(x(1 downto 0));
      v := peek(x(2));
      -- the actual is a static signal name (4.2.2.3, 8.1): its indexes are
      -- static expressions, here a constant and a pure function of literals
      elem(x(c));
      part(x(c + 1 downto c));
      elem(x(twice(1)));
    end procedure;
  begin
    outer(sv);
    wait;
  end process;
end architecture;
