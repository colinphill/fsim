-- fsim: top=d8_generic_package_port_type(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.5.5, 6.5.6.2: a port whose type is selected through an
-- interface package (`pkg.t`) has the type of the instance bound at
-- elaboration, so the actual of a port map is not typed by the template's
-- type: the signal of the instance's own type is a legal actual.
package d8_gp_template is
    generic (type t);
    type parts is (re, im);
    type pair is array (parts) of t;
end package;

package d8_gp_int is new work.d8_gp_template generic map (t => integer);

entity d8_gp_copy is
    generic (package pkg is new work.d8_gp_template generic map (<>));
    port (a : in pkg.pair; c : out pkg.pair);
end entity;

architecture arch of d8_gp_copy is
begin
    c <= a;
end architecture;

use work.d8_gp_int.all;
entity d8_generic_package_port_type is
end entity;

architecture test of d8_generic_package_port_type is
    signal x, y : work.d8_gp_int.pair := (3, 4);
begin
    u : entity work.d8_gp_copy
        generic map (pkg => work.d8_gp_int)
        port map (a => x, c => y);
    process
    begin
        x <= (5, 6);
        wait for 1 ns;
        assert y(re) = 5 and y(im) = 6 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
