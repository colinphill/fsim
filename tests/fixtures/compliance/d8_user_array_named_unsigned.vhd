-- fsim: top=d8_user_array_named_unsigned(tb) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.3, 9.3.6: a design's own array type named UNSIGNED (no
-- library is used) is that type, closely related to BIT_VECTOR through its
-- BIT elements.
entity d8_user_array_named_unsigned is
end entity;

architecture tb of d8_user_array_named_unsigned is
    type unsigned is array (integer range <>) of bit;
    signal data : unsigned(0 to 7) := x"A5";
    signal bv : bit_vector(0 to 7);
begin
    process
    begin
        bv <= bit_vector(data);
        wait for 1 ns;
        assert bv = x"A5" severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
