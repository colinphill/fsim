-- fsim: top=d8_alias_element_slice(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 4.3.3.1 / 1076-2019 6.6.2: an object alias without a subtype
-- indication denotes the named object, which may be an element, a slice or a
-- field; its type is that of the named part, not of the whole prefix.
entity d8_alias_element_slice is
end entity;

architecture test of d8_alias_element_slice is
    type rec is record
        q : integer;
        w : bit_vector(3 downto 0);
    end record;
    signal x : bit_vector(7 downto 0) := "10000011";
    signal r : rec := (q => 5, w => "0110");
    alias top : bit is x(7);
    alias low : bit_vector(1 downto 0) is x(1 downto 0);
    alias sq is r.q;
    alias rw is r.w;
begin
    process
    begin
        assert top = '1' severity failure;
        assert low = "11" severity failure;
        assert sq = 5 severity failure;
        assert rw(2) = '1' severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
