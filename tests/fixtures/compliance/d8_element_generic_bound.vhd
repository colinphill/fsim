-- fsim: top=d8_element_generic_bound(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 5.3.2.2, 6.3: an array subtype indication may constrain the
-- element subtype (here UNSIGNED, a subtype of the IEEE vector type), with a
-- bound that depends on a generic; the element's length is that bound.
library ieee;
use ieee.numeric_std.all;
use ieee.std_logic_1164.all;

package d8_egb_pkg is
  type flat_matrix is array (integer range <>) of unsigned;
end package;

library ieee;
use ieee.numeric_std.all;
use work.d8_egb_pkg.all;
entity d8_egb_unit is
  generic (N : natural := 4; W : natural := 8);
  port (a_i : in flat_matrix(0 to N - 1)(W - 1 downto 0));
end entity;
architecture a of d8_egb_unit is
  signal s : flat_matrix(0 to N - 1)(W - 1 downto 0) := (others => to_unsigned(5, W));
begin
  process begin
    wait for 1 ns;
    assert a_i(0)'length = W severity failure;
    assert a_i(N - 1)'left = W - 1 and a_i(N - 1)'right = 0 severity failure;
    assert s(1)'length = W and to_integer(s(N - 1)) = 5 severity failure;
    assert to_integer(a_i(2)) = 9 severity failure;
    report "PASS";
    wait;
  end process;
end architecture;

library ieee;
use ieee.numeric_std.all;
use work.d8_egb_pkg.all;
entity d8_element_generic_bound is
end entity;
architecture test of d8_element_generic_bound is
  signal m : flat_matrix(0 to 3)(7 downto 0) := (others => to_unsigned(9, 8));
begin
  u : entity work.d8_egb_unit port map (a_i => m);
end architecture;
