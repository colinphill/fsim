-- fsim: top=d8_alias_type_context_reference(a) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 6.3, 6.6.2, 13.4: a package subtype and a type alias of
-- STD_ULOGIC_VECTOR, visible to the package only through a context
-- reference, denote that type; a signal of them in a unit that also sees
-- IEEE only through the context reference has its layout.
library ieee;
context ieee.ieee_std_context;
package d8_atcr_types is
  alias logic_vec is std_ulogic_vector;
  subtype logic_byte is std_ulogic_vector(7 downto 0);
end package;

library ieee;
context ieee.ieee_std_context;
use work.d8_atcr_types.all;
entity d8_alias_type_context_reference is
end entity;

architecture a of d8_alias_type_context_reference is
  signal foo : logic_vec (7 downto 0) := (others => '0');
  signal bar : logic_byte := (others => '0');
begin
  foo <= x"ab" after 10 ns;
  bar <= x"5c" after 10 ns;
  process
  begin
    wait for 20 ns;
    assert foo = x"ab" severity failure;
    assert bar = x"5c" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
