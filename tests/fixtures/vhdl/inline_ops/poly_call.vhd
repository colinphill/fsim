-- SPDX-License-Identifier: Apache-2.0
--
-- One function called with arguments of two widths from a process that the
-- v4 kernel compiles (it waits on a signal). The formals and result of rev
-- are registers that the 8-bit and the 16-bit call give values of different
-- widths; each call site gets its own copy of the callee in the compiled
-- code (no member left off the compiled kernel for register_width).
-- rev reverses the elements of its argument (IEEE 1076-2019 9.3.2: the
-- result of a function over an unconstrained argument has the argument's
-- range; the loop assigns element i of the result from element
-- v'length - 1 - i of the argument, counting from the left):
--   a8  = "10110001"      rev(a8)  = "10001101"
--   a16 = "1100101011110000" rev(a16) = "0000111101010011"
-- go becomes '1' at 1 ns; the reports are at 2 ns.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal go : std_logic := '0';
  signal a8 : std_logic_vector(7 downto 0) := "10110001";
  signal a16 : std_logic_vector(15 downto 0) := "1100101011110000";
  signal r8 : std_logic_vector(7 downto 0) := (others => '0');
  signal r16 : std_logic_vector(15 downto 0) := (others => '0');

  function rev(v : std_logic_vector) return std_logic_vector is
    variable r : std_logic_vector(v'length - 1 downto 0);
    variable w : std_logic_vector(v'length - 1 downto 0);
  begin
    w := v;
    for i in 0 to v'length - 1 loop
      r(i) := w(v'length - 1 - i);
    end loop;
    return r;
  end function;
begin
  go <= '1' after 1 ns;

  worker : process (go)
  begin
    if go = '1' then
      r8 <= rev(a8);
      r16 <= rev(a16);
    end if;
  end process;

  reporter : process
  begin
    wait for 2 ns;
    report "wide poly8=" & to_string(r8) & ";";
    report "wide poly16=" & to_string(r16) & ";";
    wait;
  end process;
end architecture;
