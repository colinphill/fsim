-- fsim: top=d8_nested_package_use(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 4.7, 12.4: a package declared in a package declarative part
-- is one of its declarations; `use work.outer.all` makes it visible, and
-- inside the outer package it is visible after its declaration.
package d8_outer is
    package inner is
        generic ( N : integer );
    end package;
    package inner_in_outer is new inner generic map ( 7 );
end package;

entity d8_nested_package_use is
end entity;

use work.d8_outer.all;

architecture test of d8_nested_package_use is
    package inner4 is new inner generic map ( 4 );
begin
    p1 : process is
    begin
        assert inner4.n = 4 severity failure;
        assert inner_in_outer.n = 7 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
