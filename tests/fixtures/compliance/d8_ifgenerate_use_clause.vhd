-- fsim: top=d8_ifgenerate_use_clause(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 11.8, 12.4: a use clause in the declarative part of an
-- if-generate statement is in force for the declarations after it. The
-- clause names all declarations of the package: a clause naming only the
-- package would not make its "=" visible (12.4).
package d8_gen_pkg is
    type stats_type is range 0 to 9;
end package;

entity d8_ifgenerate_use_clause is
end entity;

architecture test of d8_ifgenerate_use_clause is
begin
    g : if true generate
        use work.d8_gen_pkg.all;
        signal s : stats_type := 3;
    begin
        p1 : process is
        begin
            assert s = 3 severity failure;
            report "PASS";
            wait;
        end process;
    end generate;
end architecture;
