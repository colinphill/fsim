-- fsim: top=d8_alias_implicit_operation(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.6.3: a subprogram alias with a signature denotes the
-- implicitly declared operation (5.2.6, 5.3.2.4) or STANDARD function whose
-- parameter and result type profile the signature matches.
entity d8_alias_implicit_operation is
end entity;

architecture test of d8_alias_implicit_operation is
    type color is (red, green);
    alias img is to_string [bit return string];
    alias cimg is to_string [color return string];
    alias bvimg is to_string [bit_vector return string];
begin
    process
        variable v : bit_vector(1 downto 0) := "10";
    begin
        assert img('1') = "1" severity failure;
        assert cimg(green) = "green" severity failure;
        assert bvimg(v) = "10" severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
